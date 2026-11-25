# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
translate_items.py — перевод оставшихся названий предметов по правилам.

ПОЧЕМУ НЕ АВТОПОДСТАНОВКА ИЗ КОРПУСА: Фантазиум переиспользовал коды предметов, поэтому пары
«англ. оригинал → рус. клиент» местами относятся к РАЗНЫМ вещам (Snowman Token → «Пиратские
ботинки»). Переводим по словарю с проверкой по типу предмета.

ЗАЩИТА ОТ ОШИБОК:
  * часть одежды сверяется с ItemType (слотом): 35 голова, 36 верх, 37 низ, 38 обувь,
    39 перчатки. Если слово в названии противоречит слоту — запись НЕ переводится, а попадает
    в отчёт (лучше оставить английское, чем назвать штаны шляпой);
  * если в названии остаётся хоть одно неизвестное словарю слово — запись НЕ переводится;
  * темы, уже встречающиеся в переводе игры (Ханбок, Пальмира, Династия, Дикий Запад),
    берутся в существующих формах — ради единообразия с тем, что игрок уже видит.
"""
import os, io, sys, re, struct, zipfile, hashlib, shutil, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from corpus import read_name
from langdet import lang

CLIENT_BBZ = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\Character\Bin.bbz"
BACKUP = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\out_client\Data\Character\Bin.bbz.items_bak"
REPORT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\translate_items.txt"

SLOT = {35: "head", 36: "top", 37: "bottom", 38: "feet", 39: "hands"}

# ЧАСТИ: слово -> (перевод, ожидаемый слот)
PARTS = {
    "hat": ("Шляпа", "head"), "headband": ("Повязка", "head"), "helmet": ("Шлем", "head"),
    "halo": ("Нимб", "head"), "horns": ("Рожки", "head"), "head": ("Головной убор", "head"),
    "cap": ("Кепка", "head"), "elmo": ("Шлем", "head"), "mask": ("Маска", "head"),
    "ears": ("Ушки", "head"), "hairband": ("Обруч", "head"),
    "top": ("Верх", "top"), "coat": ("Куртка", "top"), "chest": ("Нагрудник", "top"),
    "armor": ("Броня", "top"), "shirt": ("Рубаха", "top"), "cuirass": ("Кираса", "top"),
    "peitoral": ("Нагрудник", "top"), "jacket": ("Куртка", "top"), "robe": ("Мантия", "top"),
    "bottom": ("Низ", "bottom"), "bottoms": ("Низ", "bottom"), "pants": ("Штаны", "bottom"),
    "tasset": ("Набедренник", "bottom"), "skirt": ("Юбка", "bottom"),
    "shoes": ("Ботинки", "feet"), "boots": ("Ботинки", "feet"), "botas": ("Ботинки", "feet"),
    "gloves": ("Перчатки", "hands"), "manopla": ("Перчатки", "hands"),
    "muff": ("Муфта", None), "tail": ("Хвост", None), "wings": ("Крылья", None),
    # добавлено по остатку: обувь, головные уборы, кисти
    "slippers": ("Тапочки", "feet"), "clogs": ("Башмаки", "feet"),
    "moccasins": ("Мокасины", "feet"), "sandals": ("Сандалии", "feet"),
    "hood": ("Капюшон", "head"), "headdress": ("Головной убор", "head"),
    "hair": ("Причёска", "head"), "turban": ("Тюрбан", "head"),
    "mittens": ("Рукавицы", "hands"),
}
# ТЕМЫ: фраза -> форма в родительном падеже (порядок важен: длинные раньше коротких)
THEMES = [
    ("cool wild west", "с дикого запада (стильный)"), ("wild west", "с дикого запада"),
    ("cool hanbok", "ханбока (стильный)"), ("hanbok", "ханбока"),
    ("cool japanese", "японский (стильный)"), ("japanese", "японский"),
    ("cool dynasty", "династии (стильный)"), ("dynasty hero", "героя династии"), ("dynasty", "династии"),
    ("cool palmyra's", "пальмиры (стильный)"), ("cool palmyra", "пальмиры (стильный)"),
    ("palmyra's", "пальмиры"), ("palmyra", "пальмиры"),
    ("western hero", "героя запада"), ("desert hero", "героя пустыни"),
    ("rising sun", "восходящего солнца"),
    ("high butler", "старшего дворецкого"), ("special butler", "особого дворецкого"), ("butler", "дворецкого"),
    ("maid", "служанки"),
    ("ruby celestine", "рубиновой небожительницы"), ("emerald celestine", "изумрудной небожительницы"),
    ("celestine", "небожительницы"),
    ("lovely bunny", "милого кролика"), ("cute bunny", "милого кролика"),
    ("fuzzy bunny", "пушистого кролика"), ("fluffy bunny", "пушистого кролика"),
    ("white rabbit", "белого кролика"), ("bunny", "кролика"), ("de coelho", "кролика"),
    ("veteran reindeer", "оленя-ветерана"), ("captain reindeer", "оленя-капитана"),
    ("elite reindeer", "элитного оленя"), ("reindeer", "оленя"),
    ("jolly snowman", "весёлого снеговика"), ("holly snowman", "праздничного снеговика"),
    ("snowy wood", "снежного леса"), ("snowman", "снеговика"),
    ("angel costume", "костюма ангела"), ("devil costume", "костюма дьявола"),
    ("special devil", "особого дьявола"), ("angel", "ангела"), ("devil", "дьявола"),
    ("shi-no-bi", "ниндзя"), ("shinobi", "ниндзя"),
    ("hero christmas", "рождественского героя"), ("christmas", "рождественский"),
    # страны и народы (национальные костюмы)
    ("hero of japan", "героя Японии"), ("hero of korea", "героя Кореи"),
    ("hero of china", "героя Китая"), ("hero of egypt", "героя Египта"),
    ("hero of america", "героя Америки"), ("hero of holland", "героя Голландии"),
    # национальные наряды — форма в родительном падеже, чтобы фраза читалась:
    # «Шляпа голландского наряда», а не «Шляпа голландский»
    ("dutch", "голландского наряда"), ("indian", "индейского наряда"),
    ("spanish", "испанского наряда"), ("polynesian", "полинезийского наряда"),
    ("korean", "корейского наряда"), ("chinese", "китайского наряда"),
    ("egyptian", "египетского наряда"), ("american", "американского наряда"),
    ("mexican", "мексиканского наряда"),
    ("gloomy", "мрачного костюма"), ("fresh", "свежего костюма"),
    # добавлено по остатку
    ("native american", "индейского наряда"), ("military", "военного костюма"),
    ("cool international", "международного наряда (стильный)"),
    ("international", "международного наряда"),
    ("high butler maid", "старшего дворецкого"), ("butler maid", "дворецкого"),
    ("spanish costume", "испанского наряда"),
    ("hero", "героя"),
]

# РАСХОДНИКИ И ПРОЧЕЕ: перевод по составу слов (слот не проверяется, ItemType 46 и др.)
# РАСХОДНИКИ переводятся ТОЧНЫМИ фразами, а не сборкой из слов: пословная склейка давала
# неграмотный результат («Свиток доспехов восстановления», «Набор золота физической плюс»).
PHRASES = {
    "armor restore scroll": "Свиток восстановления доспехов",
    "weapon restore scroll": "Свиток восстановления оружия",
    "exp potion": "Зелье опыта",
    "hp potion": "Зелье здоровья",
    "mp potion": "Зелье магии",
    "hp elixir": "Эликсир здоровья",
    "mp elixir": "Эликсир магии",
    "gold upgrade package": "Набор улучшения за золото",
    "armor upgrade package": "Набор улучшения доспехов",
    "weapon upgrade package": "Набор улучшения оружия",
    "additional upgrades": "Дополнительные улучшения",
    "successful upgrades": "Удачные улучшения",
    "package-additional upgrades": "Набор дополнительных улучшений",
    "package-successful upgrades": "Набор удачных улучшений",
    "absolute crystal package": "Набор абсолютных кристаллов",
    "fishing equipment package": "Набор рыболовных снастей",
    "crafting package": "Ремесленный набор",
    "abominable stone": "Камень мерзости",
    "abomination summon stone": "Камень вызова мерзости",
    "abominations soul": "Душа мерзости",
    "account faction reset": "Сброс фракции аккаунта",
    "anniversary booster": "Юбилейный бустер",
    "anniversary booster shard": "Осколок юбилейного бустера",
    "anniversary metal piece": "Юбилейный металлический фрагмент",
    "anniversary stone piece": "Юбилейный каменный фрагмент",
    "anniversary wooden piece": "Юбилейный деревянный фрагмент",
    "anniversary sowel box": "Юбилейная шкатулка с живыми камнями",
    "anniversary ticket": "Юбилейный билет",
    "anniversary title token": "Юбилейный жетон звания",
    "angel set package": "Набор костюма ангела",
    "devil set package": "Набор костюма дьявола",
    "bunny costume set": "Набор костюма кролика",
    "package-korean set": "Набор корейского наряда",
    "package-american set": "Набор американского наряда",
    "package-japanese set": "Набор японского наряда",
    "package-chinese set": "Набор китайского наряда",
    "package-egyptian set": "Набор египетского наряда",
    "banana milkshake title token": "Жетон звания «Банановый коктейль»",
    "christmas gift box": "Коробка с рождественским подарком",
    "pet limit-breaking charm": "Амулет разрушения предела питомца",
}

GOODS_HEAD = {                      # ключевое слово-предмет -> перевод
    "potion": "Зелье", "scroll": "Свиток", "elixir": "Эликсир", "stone": "Камень",
    "soul": "Душа", "coupon": "Купон", "ticket": "Билет", "token": "Жетон",
    "piece": "Часть", "shard": "Осколок", "box": "Коробка", "crystal": "Кристалл",
    "gift": "Подарок", "package": "Набор", "set": "Набор", "booster": "Бустер",
    "mittens": "Рукавицы", "milkshake": "Молочный коктейль", "egg": "Яйцо",
    "charm": "Амулет", "food": "Еда", "sowel": "Живой камень", "bonus": "Бонус",
}
GOODS_MOD = {                       # определения
    "hp": "здоровья", "mp": "магии", "exp": "опыта", "gold": "золота",
    "attack": "атаки", "physical": "физической", "magical": "магической",
    "element": "стихии", "restore": "восстановления", "upgrade": "улучшения",
    "upgrades": "улучшения", "additional": "дополнительный", "anniversary": "юбилейный",
    "summon": "вызова", "abomination": "мерзости", "abominable": "мерзкий",
    "abominations": "мерзости", "absolute": "абсолютный", "pet": "питомца",
    "title": "звания", "option": "опции", "increase": "увеличения",
    "successful": "удачного", "transfer": "переноса", "leveling": "прокачки",
    "armor": "доспехов", "weapon": "оружия", "faction": "фракции", "reset": "сброса",
    "double": "двойной", "plus": "плюс", "fishing": "рыбака", "crafting": "ремесленный",
}
SEP = re.compile(r"[\s\-,]+")

def norm(s):
    return SEP.sub(" ", s.strip().lower())

QTY = re.compile(r"\((\d+(?:\+\d+)?)\)|\*(\d+)|\((\d+)\s*(?:hr|hour|day|days|min)\)", re.I)

def split_suffix(name):
    """Отделить хвост с количеством/длительностью: 'Scroll(5)*2' -> ('Scroll', ' (5) x2')."""
    suffix = []
    def take(m):
        if m.group(1): suffix.append("(%s)" % m.group(1))
        elif m.group(2): suffix.append("x%s" % m.group(2))
        elif m.group(3): suffix.append("(%s)" % m.group(3))
        return " "
    base = QTY.sub(take, name)
    return base, ((" " + " ".join(suffix)) if suffix else "")

def translate_goods(low):
    """Расходники: ТОЛЬКО точное совпадение фразы. Пословную сборку не делаем — она даёт
    неграмотные названия, а неверный перевод хуже английского оригинала."""
    key = low.strip()
    if key in PHRASES:
        return PHRASES[key], None
    return None, "нет точной фразы в словаре"

def translate(name, itype):
    """-> (перевод, причина отказа). Переводим только полностью разобранные названия."""
    if "[" in name or "?" in name or "NONE" in name.upper():
        return None, "служебное/событийное название"
    base, suffix = split_suffix(name)
    low = norm(base)
    if not low:
        return None, "пустое название"
    # часть одежды: ищем известное слово
    slot = SLOT.get(itype)
    part = None
    for w in low.split():
        if w in PARTS:
            # часть одежды учитываем только если предмет действительно носится
            want = PARTS[w][1]
            if slot is None and want is not None:
                continue                     # ItemType 46 и т.п. — это не одежда
            part = w; break
    if part is not None:
        ru_part, want = PARTS[part]
        if want and slot and want != slot:
            return None, "слово '%s' противоречит слоту %s" % (part, slot)
        rest = " ".join(w for w in low.split() if w != part)
        if not rest:
            return ru_part + suffix, None
        for en, ru in THEMES:
            if en in rest:
                leftover = norm(rest.replace(en, " "))
                if leftover:
                    return None, "нераспознанный остаток '%s'" % leftover
                return "%s %s%s" % (ru_part, ru, suffix), None
        return None, "неизвестная тема '%s'" % rest
    ru, why = translate_goods(low)
    return (ru + suffix, None) if ru else (None, why)

def main():
    apply_it = "--apply" in sys.argv
    zin = zipfile.ZipFile(CLIENT_BBZ)
    data = bytearray(zin.read("item_list.bin"))
    cnt = struct.unpack_from("<i", data, 4)[0]
    done, skip = [], []
    seen = {}
    for i in range(cnt):
        o = 20 + i * 264
        body = bytearray(data[o + 16:o + 264])
        nm = read_name(body, True)
        if not nm.strip() or lang(nm) != "lat":
            continue
        en = nm.decode("cp1251", "replace")
        itype = struct.unpack_from("<i", body, 128)[0]
        ru, why = translate(en, itype)
        if ru is None:
            skip.append("%s\t%s\t%s" % (en, itype, why)); continue
        t = ru.encode("cp1251")
        if len(t) + 1 > 64:
            skip.append("%s\t%s\tне влезает" % (en, itype)); continue
        # ШИФР ОБЯЗАТЕЛЕН: клиент читает имя как «байт-1», поэтому пишем «байт+1».
        # Без этого в игре текст съезжает на байт («Дополнительные» -> «Гнонкмзсдкымъд»).
        body[4:68] = bytes(((c + 1) & 0xFF) for c in t) + b"\x01" * (64 - len(t))
        data[o + 16:o + 264] = body
        data[o:o + 16] = hashlib.md5(bytes(body)).digest()
        done.append("%s\t%s" % (en, ru))
        seen[en] = ru
    print("переведено записей: %d | пропущено: %d" % (len(done), len(skip)))
    print("уникальных переводов: %d\n" % len(seen))
    for en, ru in list(seen.items())[:12]:
        print("   %-34s -> %s" % (en, ru))
    reasons = collections.Counter(s.split("\t")[-1].split(" '")[0] for s in skip)
    print("\nпричины пропуска:")
    for r, n in reasons.most_common(8): print("   %-40s %d" % (r, n))
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with io.open(REPORT, "w", encoding="utf-8-sig") as f:
        f.write("# Перевод названий предметов по правилам\n# ПЕРЕВЕДЕНО (англ\\tрус)\n")
        f.write("\n".join(sorted(set(done))))
        f.write("\n\n# ПРОПУЩЕНО (англ\\tItemType\\tпричина)\n")
        f.write("\n".join(sorted(set(skip))))
    if not apply_it:
        zin.close(); print("\nсухой прогон. Применить: --apply"); return
    if not done:
        zin.close(); print("нечего применять"); return
    if not os.path.isfile(BACKUP):
        os.makedirs(os.path.dirname(BACKUP), exist_ok=True); shutil.copy2(CLIENT_BBZ, BACKUP)
    tmp = CLIENT_BBZ + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            payload = bytes(data) if info.filename == "item_list.bin" else zin.read(info)
            zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            zi.compress_type = zipfile.ZIP_DEFLATED; zi.external_attr = info.external_attr
            zout.writestr(zi, payload)
    zin.close(); os.replace(tmp, CLIENT_BBZ)
    print("\nприменено. Бэкап: %s" % BACKUP)
    print("отчёт: %s" % REPORT)

if __name__ == "__main__":
    main()
