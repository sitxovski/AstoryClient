# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
merge_mob_names.py — перенос имён мобов и боссов из Фантазиума.

Таблица: Data\Mob\Mob list.BIN — заголовок 20 Б (magic 'MAL'), записи по 272 Б.
Раскладка (установлена дампом + побайтовым сравнением клиентов, 851 общая запись):
    +0   i32    id моба (1-based, резолв: row = id - 1)
    +4   char32 имя модели
    +36  char32 имя модели (то же; это поле читает рантайм — см. память mob-ue-actors)
    +68  char32 имя модели варианта
    +100 char32 ОТОБРАЖАЕМОЕ ИМЯ         <- переносим только это
    +148 ..     числовые данные (различаются у 554 записей между сборками) — НЕ ТРОГАЕМ
    +212 char32 имя иконки ('mon_icon_01'); байты +224..+242 — мусор внутри этого поля

Поле имени копируется ЦЕЛИКОМ (32 байта) — так запись физически не может выйти за его границы
и затронуть соседние поля (ошибка, из-за которой раньше терялись имена моделей NPC).
Ключ сопоставления — id, а не позиция: у клиента 854 записи, у Фантазиума 851.
"""
import os, io, sys, struct, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from langdet import lang

CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\Mob\Mob list.BIN"
FANTA = r"@@ASTORY_DEV_ROOT@@\Donors\Fantazium\data\Mob\Mob list.BIN"
BACKUP_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\out_client\Data\Mob"
REPORT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\mob_names.txt"

STRIDE, BASE = 272, 20
NAME_OFF, NAME_CAP = 100, 32

def name_of(rec):
    raw = bytes(rec[NAME_OFF:NAME_OFF + NAME_CAP])
    z = raw.find(b"\x00")
    return raw[:z if z >= 0 else NAME_CAP]

def load(path):
    d = open(path, "rb").read()
    cnt = struct.unpack_from("<i", d, 4)[0]
    m = {}
    for i in range(cnt):
        o = BASE + i * STRIDE
        if o + STRIDE > len(d):
            break
        m[struct.unpack_from("<i", d, o)[0]] = (i, d[o:o + STRIDE])
    return d, cnt, m

def main():
    apply_it = "--apply" in sys.argv
    cd, ccnt, C = load(CLIENT)
    _, fcnt, F = load(FANTA)
    data = bytearray(cd)
    log, skipped = [], []
    n = 0
    for key, (i, rec) in C.items():
        src = F.get(key)
        if src is None:
            skipped.append("%s\tнет записи у Фантазиума\t%s" % (key, name_of(rec).decode("cp1251", "replace")))
            continue
        cur, new = name_of(rec), name_of(src[1])
        if lang(new) != "rus" or cur == new:
            continue
        o = BASE + i * STRIDE
        # копируем ПОЛЕ ЦЕЛИКОМ — за его границы выйти невозможно
        data[o + NAME_OFF:o + NAME_OFF + NAME_CAP] = src[1][NAME_OFF:NAME_OFF + NAME_CAP]
        log.append("%s\t%s\t%s" % (key, cur.decode("cp1251", "replace"), new.decode("cp1251", "replace")))
        n += 1
    print("записей: клиент=%d Фантазиум=%d | переведено=%d | без пары=%d" % (ccnt, fcnt, n, len(skipped)))
    for l in log[:8]:
        k, a, b = l.split("\t")
        print("   id=%-5s '%s' -> '%s'" % (k, a, b))
    if not apply_it:
        print("\nсухой прогон. Применить: --apply")
        return
    # контроль: изменились ТОЛЬКО байты поля имени
    for i in range(ccnt):
        o = BASE + i * STRIDE
        a, b = cd[o:o + STRIDE], bytes(data[o:o + STRIDE])
        if a[:NAME_OFF] != b[:NAME_OFF] or a[NAME_OFF + NAME_CAP:] != b[NAME_OFF + NAME_CAP:]:
            raise AssertionError("запись %d: изменены байты ВНЕ поля имени" % i)
    os.makedirs(BACKUP_DIR, exist_ok=True)
    bak = os.path.join(BACKUP_DIR, os.path.basename(CLIENT))
    if not os.path.isfile(bak):
        shutil.copy2(CLIENT, bak)
        print("резервная копия: %s" % bak)
    open(CLIENT, "wb").write(bytes(data))
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with io.open(REPORT, "w", encoding="utf-8-sig") as f:
        f.write("# Имена мобов: перенесено %d\n# id\tбыло\tстало\n" % n)
        f.write("\n".join(log))
        if skipped:
            f.write("\n\n# Без пары в Фантазиуме\n" + "\n".join(skipped))
    print("применено. отчёт: %s" % REPORT)

if __name__ == "__main__":
    main()
