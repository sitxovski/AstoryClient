# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
translate_small.py — перевод небольших таблиц, которых нет в русском источнике.

Термины взяты НЕ с потолка, а из уже переведённых данных клиента:
  * названия локаций  — из Data\NPC\TIM_List.BIN (Силарис, Река Дождя, Альпия, Пламения,
    Солнечный Берег, Забытая Лощина, Долина Ночи, Страна Вечных Войн) и из реплик NPC
    (Дворец Королевы, Шахматный замок, Замок Хаоса, Логово Дракона, Гора Пламень,
    Огненная Чаща, Поле битвы);
  * ветки классов     — из jobtype.JobName (Новичок / Лучник / Маг; «N профессия»);
  * кнопки туториала  — из уже переведённого Tutorial_text_answer.bin (те же 4 подписи);
  * названия рецептов и товаров — из item_list (Духовное ожерелье барона, Ожерелье
    выносливости графа, Волшебное кольцо виконта, Кольцо защиты/разрушения, Свиток
    возвращения/оживления, Зелье опыта питомца, Бустер …).

Bpremium вообще не переводится вручную: его ключ — код предмета, поэтому русское название
берётся напрямую из item_list.

Запись безопасная: поле не расширяется за доступное место (длина текущей строки плюс идущие
за её терминатором нули), и после записи проверяется, что байты ВНЕ поля не изменились.
"""
import os, io, sys, struct, zipfile, shutil

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from langdet import lang

CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
BACKUP = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\out_client"
REPORT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\translate_small.txt"

# ---------------------------------------------------------------- локации
MAP_INFO = {
    0: "Силарис", 1: "Река Дождя", 2: "Страна Вечных Войн", 3: "Альпия",
    4: "Долина Ночи", 5: "Забытая Лощина", 6: "Солнечный Берег", 7: "Пламения",
    8: "Дворец Королевы", 9: "Шахматный замок",
    10: "Дворец Королевы (турнир)", 11: "Шахматный замок (турнир)",
    12: "Гора Пламень", 13: "Флабис", 14: "Мёртвая пустыня", 15: "Логово Дракона",
    16: "Огненная Чаща", 17: "Замок Хаоса", 18: "Инферион", 19: "Поле битвы",
    20: "Поле событий", 21: "Поле чести", 22: "Викторина",
}
# ---------------------------------------------------------------- ветки классов
PARENT_JOB = {
    "Beginner": "Новичок", "Fighter": "Боец", "Archer": "Лучник", "Mage": "Маг",
    "Second Profession": "Вторая профессия",
    "1st Profession": "1 профессия", "2nd Profession": "2 профессия",
    "3rd Profession": "3 профессия", "4th Profession": "4 профессия",
    "5th Profession": "5 профессия",
}
# ------------------------------------------- кнопки туториала (как в Tutorial_text_answer)
TUTORIAL_MID = {
    1: "<FC=FFFFFF96>Назад</F>",
    2: "<FC=FFB4FBFF>Далее</F>",
    3: "<FC=FFACFFEF>Заново (вернуться к началу)</F>",
    4: "<FC=FFFFB0CF>КОНЕЦ</F>",
}
# ---------------------------------------------------------------- рецепты
RECIPES = {
    233: "Духовное ожерелье барона",   234: "Духовное ожерелье виконта",  235: "Духовное ожерелье графа",
    236: "Ожерелье выносливости барона", 237: "Ожерелье выносливости виконта", 238: "Ожерелье выносливости графа",
    239: "Волшебное кольцо барона",    240: "Волшебное кольцо виконта",    241: "Волшебное кольцо графа",
    242: "Кольцо защиты барона",       243: "Кольцо защиты виконта",       244: "Кольцо защиты графа",
    245: "Кольцо разрушения барона",   246: "Кольцо разрушения виконта",   247: "Кольцо разрушения графа",
    248: "Перевязочный набор зонтика",
}
# ---------------------------------------------------------------- премиум-магазин (Spremium)
SPREMIUM = {
    434: "Кошачий шарик (1 год)",        435: "Круговой байк (1 год)",
    432: "Летающий ковёр (1 год)",       436: "Пламенные колёса (1 год)",
    108: "Цито XT (1 год)",              403: "Особая еда (3 дня)",
    405: "Ускоритель роста (24 ч)",      398: "Яйцо Доги",
    404: "Амулет разрушения предела питомца",
    322: "Добавка для вылупления питомца (1)",
    458: "Свиток обмена опций (5)",      459: "Успешный перенос опций (5)",
    378: "Зелье шанса выпадения (1 ч)",  58:  "Зелье опыта (30 мин)",
    202: "Свиток телепортации (5)",      203: "Свиток телепортации (10+1)",
    204: "Свиток телепортации (20+4)",   460: "Свиток вызова склада (30)",
    205: "Премиум свиток оживления (5)", 206: "Премиум свиток оживления (10+1)",
    207: "Премиум свиток оживления (20+4)",
    325: "Набор зелий опыта (3)",        326: "Набор зелий опыта (6+1)",
    327: "Набор зелий опыта (9+2)",      461: "Зелье регенерации (100)",
    462: "Увеличение физ. атаки (20)",   463: "Увеличение маг. атаки (20)",
    # ОЗ/ОМ — обозначения из error list.bin (loc 11031/11032); «эликсир» — как в item_list
    464: "Зелье увеличения ОЗ на 20% (3)",
    465: "Зелье увеличения ОМ на 20% (3)",
    466: "Эликсир здоровья (100)",
}

def cstr(b, off, cap):
    raw = bytes(b[off:off + cap]); z = raw.find(b"\x00")
    return raw[:z if z >= 0 else cap]

def avail(b, off, limit):
    end = off
    while end < off + limit and b[end] != 0: end += 1
    z = end
    while z < off + limit and b[z] == 0: z += 1
    return (end - off) + (z - end)

def put(b, off, limit, text):
    if len(text) + 1 > avail(b, off, limit):
        return False
    b[off:off + len(text)] = text
    b[off + len(text)] = 0
    return True

def process_loose(rel, stride, base, keyfmt, keyoff, txtoff, mapping, log, skip, fixed_cap=None):
    p = os.path.join(CLIENT, "Data", rel.replace("/", os.sep))
    if not os.path.isfile(p):
        skip.append("%s\tнет файла" % rel); return 0
    d = bytearray(open(p, "rb").read())
    orig = bytes(d)
    cnt = struct.unpack_from("<I", d, 20 if base == 0x18 else 4)[0]
    limit = fixed_cap if fixed_cap else stride - txtoff
    n = 0
    for i in range(cnt):
        o = base + i * stride
        if o + stride > len(d): break
        rec = memoryview(d)[o:o + stride]
        k = struct.unpack_from(keyfmt, d, o + keyoff)[0]
        ru = mapping.get(k)
        if ru is None: continue
        cur = cstr(rec, txtoff, limit)
        if lang(cur) == "rus": continue
        b = bytearray(rec)
        if not put(b, txtoff, limit, ru.encode("cp1251")):
            skip.append("%s\t%s\tне влезает (%d б)\t%s" % (rel, k, avail(b, txtoff, limit), ru)); continue
        d[o:o + stride] = b
        log.append("%s\t%s\t%s\t%s" % (rel, k, cur.decode("cp1251", "replace"), ru))
        n += 1
    if n:
        # контроль: вне текстового поля ничего не поехало
        for i in range(cnt):
            o = base + i * stride
            if o + stride > len(d): break
            if orig[o:o + txtoff] != bytes(d[o:o + txtoff]) or \
               orig[o + txtoff + limit:o + stride] != bytes(d[o + txtoff + limit:o + stride]):
                raise AssertionError("%s запись %d: изменены байты вне поля" % (rel, i))
        bak = os.path.join(BACKUP, "Data", rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(bak), exist_ok=True)
        if not os.path.isfile(bak): shutil.copy2(p, bak)
        open(p, "wb").write(bytes(d))
    return n

def process_bpremium(log, skip):
    """Ключ Bpremium — код предмета: берём готовое русское название из item_list."""
    z = zipfile.ZipFile(os.path.join(CLIENT, "Data", "Character", "Bin.bbz"))
    il = z.read("item_list.bin"); cnt = struct.unpack_from("<i", il, 4)[0]
    items = {}
    for i in range(cnt):
        b = il[20 + i * 264 + 16:20 + (i + 1) * 264]
        code = (struct.unpack_from("<I", b, 0)[0] - 35) & 0xFFFFFFFF
        pl = bytes(((x - 1) & 0xFF) for x in b[4:68]); zp = pl.find(b"\x00")
        items[code] = pl[:zp if zp >= 0 else 64].strip()
    z.close()
    mapping = {}
    p = os.path.join(CLIENT, "Data", "NPC", "Shop", "Bpremium.BIN")
    d = open(p, "rb").read(); c = struct.unpack_from("<I", d, 4)[0]
    for i in range(c):
        k = struct.unpack_from("<i", d, 20 + i * 80)[0]
        it = items.get(k)
        if it and lang(it) == "rus":
            mapping[k] = it.decode("cp1251")
    return process_loose("NPC/Shop/Bpremium.BIN", 80, 0x14, "<i", 0, 4, mapping, log, skip)

def process_jobtype(log, skip):
    """jobtype лежит в Bin.bbz; ParentJob = ветка класса (поле +32, 32 байта)."""
    p = os.path.join(CLIENT, "Data", "Character", "Bin.bbz")
    zin = zipfile.ZipFile(p)
    d = bytearray(zin.read("jobtype.bin"))
    cnt = struct.unpack_from("<i", d, 4)[0]
    n = 0
    for i in range(cnt):
        o = 20 + i * 64
        cur = cstr(d, o + 32, 32)
        ru = PARENT_JOB.get(cur.decode("cp1251", "replace"))
        if ru is None: continue
        t = ru.encode("cp1251")
        d[o + 32:o + 64] = t + b"\x00" * (32 - len(t))
        log.append("jobtype.bin\t%d\t%s\t%s" % (i, cur.decode("cp1251", "replace"), ru))
        n += 1
    if n:
        tmp = p + ".tmp"
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                payload = bytes(d) if info.filename == "jobtype.bin" else zin.read(info)
                zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                zi.compress_type = zipfile.ZIP_DEFLATED; zi.external_attr = info.external_attr
                zout.writestr(zi, payload)
        zin.close(); os.replace(tmp, p)
    else:
        zin.close()
    return n

def main():
    log, skip = [], []
    # Map_info: имя @4; числовое поле идёт с +68 -> ёмкость 64 (граница проверяется ниже)
    mi = open(os.path.join(CLIENT, "Data", "Map_info.BIN"), "rb").read()
    assert struct.unpack_from("<i", mi, 20 + 68)[0] != 0, "ожидалось числовое поле на +68"
    res = []
    res.append(("Map_info.BIN", process_loose("Map_info.BIN", 392, 0x14, "<i", 0, 4, MAP_INFO, log, skip, fixed_cap=64)))
    res.append(("Tutorial_middle_category.bin", process_loose("Tutorial_middle_category.bin", 204, 0x14, "<i", 0, 4, TUTORIAL_MID, log, skip)))
    res.append(("making_recipe.BIN", process_loose("NPC/making_recipe.BIN", 128, 0x14, "<h", 0, 2, RECIPES, log, skip)))
    res.append(("Spremium.bin", process_loose("NPC/Shop/Spremium.bin", 76, 0x14, "<i", 0, 4, SPREMIUM, log, skip)))
    res.append(("Bpremium.BIN (из item_list)", process_bpremium(log, skip)))
    res.append(("jobtype.bin (ветки классов)", process_jobtype(log, skip)))
    print("%-32s %s" % ("таблица", "переведено"))
    for a, b in res: print("%-32s %d" % (a, b))
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with io.open(REPORT, "w", encoding="utf-8-sig") as f:
        f.write("# Перевод небольших таблиц\n# таблица\tключ\tбыло\tстало\n")
        f.write("\n".join(log))
        if skip:
            f.write("\n\n# Пропущено\n" + "\n".join(skip))
    print("\nвсего полей: %d | пропущено: %d | отчёт: %s" % (len(log), len(skip), REPORT))

if __name__ == "__main__":
    main()
