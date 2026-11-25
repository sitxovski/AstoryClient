# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_fable_mounts_data.py — ДАННЫЕ 28 новых маунтов донора AsdaFableClassic (itm1_n_96_vehicle_0053…0080)
для порта и dev-сервера: 79 предметов-транспорта (SubType 15), коды 559…637, цена 500 GXP каждый, витрина
премиум-магазина 6000, категория «Инструмент → Транспорт» (Category 1, Sub1 3, Sub2 -1 — как у 42 прежних).

ЕДИНСТВЕННЫЙ источник спецификации — список VARIANTS ниже; из него строятся ВСЕ ведомости:
  • AstoryClient/RawAssets/ItemDB/_tail_append/special_itemlist.csv        — хвост special_itemlist (DataHex 300 б)
  • AstoryClient/RawAssets/ItemDB/_tail_append/specialitem_tooltiplist.csv — хвост подсказок (Id = код)
    (обе дописывает AstoryClient/Tools/ItemDB/decode_item_db.py ключом реестра tail_ledger при каждом декоде)
  • AstoryClient/RawAssets/Data/RideEffect_tail.csv   — шлейф/звуки езды (дописывает asda_build_ride_effect_dt.py)
  • AstoryClient/RawAssets/Data/NPC/PremiumShopBin_tail.json — строки витрины (дописывает asda_import_premium_shop_bin.py)
  • AstoryClient/Tools/data/loc/en/special_itemlist.json, specialitem_tooltiplist.json — EN (новые ключи, was = null)
  • Reports/fable_mounts/server/Function_Table_rows.txt, SellItemTable_codes.txt — строки для сервера
  • Reports/fable_mounts/variants.csv — сводка для отчёта
Повторный прогон перезаписывает ведомости тем же содержимым (идемпотентно); EN-ведомости правятся точечно
(строки этого генератора помечены source = "fable_mounts", чужие строки не трогаются).

РЕШЕНИЯ ВЛАДЕЛЬЦА 23.10.2026 (MEMANTO, тип decision): цена 500 за КАЖДЫЙ вариант; скорость и сроки — НЕ общие:
  • где донор их задаёт (записи SubType 15 донора: скорость = u16 @84, срок дней = i32 @128) — как у донора;
  • 18 моделей «коллекции» донора (SubType 230; @84 там порядковый номер, а русские тексты сдвинуты) — скорость
    ПО СЕМЕЙСТВУ нашего сервера (базовая 40 / цветная 60 / золотая, «кровавая», XT, все ковры, шарики, колёса,
    куклы и цветные нимбы 80), сроки 7/30/90 дней.
Поля записи, не перечисленные в спецификации, берутся из нашей записи-образца магазинного маунта код 522
(«Шарик в виде кошки (30 дней)»: Flag101 2, LinkGroupId 5, Flag124 1, откат 30000, DropColorIndex 2 и т.д.).
Иконки — атлас и ячейка из записи донора (premium_icon3/4 импортированы 23.10 из juidata\sIcon донора).
Шлейф и звуки езды — из Ride_effect_table донора по коду донорского варианта.

Запуск (без UE):  python asda_fable_mounts_data.py
"""
import csv
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asda_paths as _P

PORT = _P.PORT_DIR
ITEMDB_LIVE = os.path.join(PORT, "AstoryClient", "RawAssets", "Data", "ItemDB")
TAIL_DIR = os.path.join(PORT, "AstoryClient", "RawAssets", "ItemDB", "_tail_append")
RIDE_TAIL = os.path.join(PORT, "AstoryClient", "RawAssets", "Data", "RideEffect_tail.csv")
SHOP_TAIL = os.path.join(PORT, "AstoryClient", "RawAssets", "Data", "NPC", "PremiumShopBin_tail.json")
LOC_EN = os.path.join(HERE, "data", "loc", "en")
REPORT = os.path.join(PORT, "Reports", "fable_mounts")
DONOR_DEC = os.path.join(REPORT, "_dec")            # расшифрованные таблицы донора (extract_fable_mounts.py)

FIRST_CODE = 559          # первый свободный код: special_itemlist порта и Function_Table сервера кончаются на 558
PRICE = 500               # GXP, решение владельца 23.10.2026
TEMPLATE_ROW = "521"      # _row записи-образца (код 522, магазинный маунт 30 дней)
SOURCE = "fable_mounts"   # метка строк этого генератора в EN-ведомостях

# ── Модели: ModelEntry → (RU-основа ≤ 21 знака, EN-основа, RU-описание, EN-описание) ──────────────────────
#   RU-имя записи = char[32] (≤ 31 байт cp1251 с суффиксом срока), поэтому основы короткие.
FLAVOR = {
    "sled": ("Сани, что носятся над снегами Альпеона в праздничную ночь. Колокольчики звенят сами собой.",
             "A festive sled that glides over the snows of Alpen on holiday nights. Its bells ring on their own."),
    "bunny": ("Надувной кролик, которого невозможно не полюбить. Мягкий, прыгучий и очень преданный.",
              "An inflatable bunny nobody can help but love. Soft, bouncy and endlessly loyal."),
    "hatch": ("Детёныш динозавруса с полей Альпеона. Редкий, своенравный, но верный своему всаднику.",
              "A young hatchling from the fields of Alpen. Rare and wilful, yet loyal to its rider."),
    "cito": ("Легендарный мотоцикл \"Цито\" из мастерских гильдии механиков. Ревёт мотором на всю округу.",
             "The legendary Cito motorcycle from the mechanics' guild workshops. Its engine roars across the land."),
    "mustang": ("Четырёхколёсный \"Мустанг\" пройдёт там, где тропы кончаются. Мощь и надёжность.",
                "A four-wheeled Mustang that goes where the trails end. Raw power you can rely on."),
    "circle": ("Унициклус — триумф баланса и равновесия, новое слово в колёсной технике.",
               "The Circle Bike, a triumph of balance and a new word in wheeled engineering."),
    "wheels": ("Тайное пылающее колесо. Оставляет за собой огненный след.",
               "A mysterious blazing wheel that leaves a trail of fire behind it."),
    "carpet": ("Волшебный ковёр-самолёт. Мягко скользит по воздуху над любыми дорогами.",
               "A magic flying carpet that glides softly through the air above any road."),
    "broom": ("Эта метла не для уборки, а для полётов. Ведьмы Альпеона ценят её выше золота.",
              "This broom is not for sweeping but for flying. The witches of Alpen prize it above gold."),
    "doll": ("Потрясающе удобный набивной дракон. Мягче облака, крепче лат.",
             "An astonishingly comfortable stuffed dragon. Softer than a cloud, sturdier than armour."),
    "catball": ("Шарик в виде кошачьей морды. Высоко не летает, зато как забавно выглядит!",
                "A balloon shaped like a cat's face. It won't fly high, but it is a joy to behold!"),
    "nimbus": ("Нимб. Облако, послушное своему всаднику, плывёт над землёй быстрее ветра.",
               "A nimbus. A cloud obedient to its rider that drifts over the land faster than the wind."),
    "board": ("Розовая парящая доска для юных искателей приключений.",
              "A pink hoverboard for young adventurers."),
}
MODELS = {
    # модель: (RU-основа, EN-основа, ключ описания)
    "0053": ("Розовая парящая доска", "Pink Hoverboard", "board"),
    "0054": ("Черный шарик-кошка", "Black Cat Balloon", "catball"),
    "0055": ("Черный нимб", "Black Nimbus", "nimbus"),
    "0056": ("Зеленые сани Рудольфа", "Green Rudolph Sled", "sled"),
    "0057": ("Синие сани Рудольфа", "Blue Rudolph Sled", "sled"),
    "0058": ("Черные сани Рудольфа", "Black Rudolph Sled", "sled"),
    "0059": ("Роз. любимый кролик", "Pink Love Bunny", "bunny"),
    "0060": ("Розовый динозаврус", "Pink Hatchling", "hatch"),
    "0061": ("Розовый \"Цито\"", "Pink Cito", "cito"),
    "0062": ("Розовый \"Мустанг\"", "Pink Mustang", "mustang"),
    "0063": ("Розовый унициклус", "Pink Circle Bike", "circle"),
    "0064": ("Розовые пыл. колеса", "Pink Flame Wheels", "wheels"),
    "0065": ("Роз. ковер-самолет", "Pink Magic Carpet", "carpet"),
    "0066": ("Черная метла", "Black Broomstick", "broom"),
    "0067": ("Ковер Хэллоуина", "Halloween Carpet", "carpet"),
    "0068": ("Черн. фигурка дракона", "Black Dragon Doll", "doll"),
    "0069": ("Черный динозаврус", "Black Hatchling", "hatch"),
    "0070": ("Темные кров. колеса", "Dark Blood Wheels", "wheels"),
    "0071": ("Черный \"Мустанг\"", "Black Mustang", "mustang"),
    "0072": ("Черный \"Цито\"", "Black Cito", "cito"),
    "0073": ("Шарик-кошка удачи", "Lucky Cat Balloon", "catball"),
    "0074": ("Милый шарик-кошка", "Lovable Cat Balloon", "catball"),
    "0075": ("Зеленый динозаврус", "Green Hatchling", "hatch"),
    "0076": ("Золотая кошка удачи", "Golden Lucky Cat Balloon", "catball"),
    "0077": ("Пылающий нимб", "Blazing Nimbus", "nimbus"),
    "0078": ("Грозовой нимб", "Stormy Nimbus", "nimbus"),
    "0079": ("Красный нимб", "Red Nimbus", "nimbus"),
    "0080": ("Нимб морского бриза", "Sea Breeze Nimbus", "nimbus"),
}

# ── Варианты: (модель, донорский код-источник визуала/иконки/шлейфа, скорость %, дней, откуда скорость/срок) ──
#   «донор» — запись SubType 15 донора (u16 @84 = скорость, i32 @128 = дни);
#   «семейство» — модель есть у донора только в коллекции (SubType 230), скорость по семейству нашего сервера.
def _donor(model, pairs):
    return [(model, code, 60, days, "донор") for code, days in pairs]

def _family(model, code, speed, why):
    return [(model, code, speed, d, "семейство: " + why) for d in (7, 30, 90)]

VARIANTS = (
    _donor("0053", [(585, 1)])
    + _donor("0054", [(612, 1), (613, 7), (614, 30), (615, 90)])
    + _donor("0055", [(616, 1), (617, 7), (618, 30), (619, 90)])
    + _family("0056", 173, 60, "сани 0030 — цветной вариант 60")
    + _family("0057", 174, 60, "сани 0030 — цветной вариант 60")
    + _family("0058", 175, 60, "сани 0030 — цветной вариант 60")
    + _family("0059", 176, 80, "кролики 0009/0010/0011 = 40/60/80, это вариант «любимого» 0011")
    + _family("0060", 177, 60, "динозаврусы 0025/0026/0027 = 40/60/80, цветной вариант")
    + _donor("0061", [(698, 1), (703, 7)])
    + _donor("0062", [(699, 1), (704, 7)])
    + _donor("0063", [(700, 1), (705, 7)])
    + _donor("0064", [(701, 1), (706, 7)])
    + _donor("0065", [(702, 1), (707, 7)])
    + _family("0066", 183, 60, "мётлы 0015/0016/0017 = 40/60/80, цветной вариант")
    + _family("0067", 184, 80, "ковры 0033/0049/0052 = 80")
    + _family("0068", 185, 80, "фигурки дракона 0034/0041/0042 = 80")
    + _family("0069", 186, 60, "динозаврусы 0025/0026/0027 = 40/60/80, цветной вариант")
    + _family("0070", 187, 80, "пылающие колёса 0037/0045/0046 = 80")
    + _family("0071", 188, 60, "\"Мустанг\" 0022/0023/0024 = 40/60/80, цветной вариант")
    + _family("0072", 189, 60, "\"Цито\" 0012/0013/0014 = 40/60/80, цветной вариант")
    + _family("0073", 194, 80, "шарики-кошки 0035/0043/0044 = 80")
    + _donor("0074", [(196, 7), (197, 30), (198, 90)])
    + _donor("0075", [(670, 7), (671, 30), (672, 90)])
    + _family("0076", 200, 80, "шарики-кошки 0035/0043/0044 = 80")
    + _family("0077", 201, 80, "цветные нимбы 0050/0051 = 80")
    + _family("0078", 202, 80, "цветные нимбы 0050/0051 = 80")
    + _family("0079", 203, 80, "цветные нимбы 0050/0051 = 80")
    + _family("0080", 204, 80, "цветные нимбы 0050/0051 = 80")
)

LEVEL_CAP = {"0053": 23}  # донор 585: Field76 = 23 («Только для уровней 1-23!»), у прочих 150


def term_ru(d):
    return {1: "1 день"}.get(d, "%d дней" % d)


def term_en(d):
    return {1: "1day"}.get(d, "%dday" % d)


def cs(b, o, n):
    return b[o:o + n].split(b"\0")[0].decode("cp1251")


def donor_tables():
    """Расшифрованные special_itemlist и Ride_effect_table донора → {код: запись}."""
    d = open(os.path.join(DONOR_DEC, "special_itemlist.bin"), "rb").read()
    n = struct.unpack_from("<I", d, 9)[0]
    spe = {struct.unpack_from("<i", d, 25 + i * 300)[0]: d[25 + i * 300:25 + (i + 1) * 300] for i in range(n)}
    r = open(os.path.join(DONOR_DEC, "Ride_effect_table.bin"), "rb").read()
    n = struct.unpack_from("<I", r, 9)[0]
    ride = {}
    for i in range(n):
        rec = r[25 + i * 272:25 + (i + 1) * 272]
        ride[struct.unpack_from("<i", rec, 0)[0]] = [cs(rec, o, 64) for o in (4, 68, 132, 196)]
    return spe, ride


def put_str(b, off, n, s):
    raw = s.encode("cp1251")
    assert len(raw) < n, "строка «%s» длиннее поля %d" % (s, n)
    b[off:off + n] = raw + b"\0" * (n - len(raw))


def main():
    sys.path.insert(0, os.path.join(PORT, "AstoryClient", "Tools", "ItemDB"))
    import decode_item_db as D   # rd() и реестр колонок — тем же кодом, что сверяет хвост при декоде
    csv.field_size_limit(1 << 30)
    # Номера строк хвоста считаются от АРХИВНОЙ части таблиц (коды < FIRST_CODE): после переноса в боевой каталог
    #   там уже лежат и строки этого хвоста — повторный прогон обязан выдать те же _row 558…636, а не сдвинуть их.
    live = [r for r in csv.DictReader(open(os.path.join(ITEMDB_LIVE, "special_itemlist.csv"), encoding="utf-8-sig"))
            if int(r["Code"]) < FIRST_CODE]
    live_tt = [r for r in csv.DictReader(open(os.path.join(ITEMDB_LIVE, "specialitem_tooltiplist.csv"), encoding="utf-8-sig"))
               if int(r["Id"]) < FIRST_CODE]
    tpl = next(r for r in live if r["_row"] == TEMPLATE_ROW)
    assert tpl["Code"] == "522" and tpl["SubType"] == "15", "образец сменился — проверить TEMPLATE_ROW"
    spe, ride = donor_tables()
    coll = {}   # модель → первый код коллекции донора (SubType 230) с этой моделью
    for c in sorted(spe):
        r = spe[c]
        if struct.unpack_from("<i", r, 80)[0] == 230 and cs(r, 230, 32).lower().startswith("itm1_n_96_vehicle_"):
            coll.setdefault(cs(r, 230, 32)[-4:], c)
    reg = None
    for name in dir(D):
        v = getattr(D, name)
        if isinstance(v, dict) and "special_itemlist.bin" in v and isinstance(v["special_itemlist.bin"], dict):
            reg = v
            break
    cols = reg["special_itemlist.bin"]["cols"]
    tcols = reg["specialitem_tooltiplist.bin"]["cols"]

    rows, trows, ride_rows, shop_rows, en_name, en_tip, srv_rows, summary = [], [], [], [], [], [], [], []
    for k, (model, dcode, speed, days, why) in enumerate(VARIANTS):
        code = FIRST_CODE + k
        row_i = len(live) + k
        ru_base, en_base, fl = MODELS[model]
        d = spe[dcode]
        assert cs(d, 230, 32).lower() == "itm1_n_96_vehicle_" + model, (dcode, model)
        if why == "донор":   # сверка: скорость и срок действительно из записи донора
            assert struct.unpack_from("<H", d, 84)[0] == speed and struct.unpack_from("<i", d, 128)[0] == days, dcode
        name_ru = "%s (%s)" % (ru_base, term_ru(days))
        name_en = "%s(%s)" % (en_base, term_en(days))
        b = bytearray(bytes.fromhex(tpl["DataHex"]))
        struct.pack_into("<i", b, 0, code)
        put_str(b, 4, 64, name_ru)
        if len(name_ru.encode("cp1251")) > 31:
            raise SystemExit("имя длиннее char[32]: %s" % name_ru)
        struct.pack_into("<i", b, 76, LEVEL_CAP.get(model, 150))
        struct.pack_into("<H", b, 84, speed)
        struct.pack_into("<i", b, 112, PRICE)
        struct.pack_into("<i", b, 128, days)
        put_str(b, 132, 32, cs(d, 132, 32))       # имя иконки
        put_str(b, 164, 32, cs(d, 164, 32))       # атлас
        b[196] = d[196]                           # ячейка атласа
        put_str(b, 230, 32, cs(d, 230, 32).lower())
        put_str(b, 262, 32, cs(d, 262, 32))       # набор анимаций седока Ch1_n_sit_NNNN
        b[294] = d[294]                           # байт @294 (дешифруется эталоном, item_inventory.c:1031): у донора и у наших
        #                                           семейств зависит от набора посадки (sit_0001/0008 → 1, прочие → 2)
        b[295] = d[295]                           # HeightOffset (подъём плашки/бабла)
        row = {"_row": row_i}
        for (off, typ, nm) in cols:
            row[nm] = D.rd(b, off, typ)
        row["DataHex"] = bytes(b).hex()
        rows.append(row)
        tip_ru = "[Увеличивает скорость на %d%%] %s" % (speed, FLAVOR[fl][0])
        tip_en = "[%d%% Moving speed increase] %s" % (speed, FLAVOR[fl][1])
        if model == "0053":
            tip_ru = "[Увеличивает скорость на %d%%] %s Только для уровней 1-23!" % (speed, FLAVOR[fl][0])
            tip_en = "[%d%% Moving speed increase] %s Levels 1-23 only!" % (speed, FLAVOR[fl][1])
        assert len(tip_ru.encode("cp1251")) < 180, tip_ru
        trows.append({"_row": len(live_tt) + k, "Id": code, "Name": name_ru, "Tooltip": tip_ru})
        # Шлейф/звуки: строка Ride_effect_table донора по коду варианта. У 698…707 и 670…672 строки нет (таблица донора
        #   кончается на коде 667) — тогда строка той же модели из коллекции донора (SubType 230; строки шлейфов, в отличие
        #   от русских имён, не сдвинуты: у всех моделей совпадают с их семейством — проверено по 23 моделям).
        fx = ride.get(dcode) or ride.get(coll.get(model)) or ["", "", "", ""]
        strip = lambda s: s.lower()[:-4] if s.lower().endswith((".jeb", ".wav")) else s.lower()
        ride_rows.append([code] + [strip(s) for s in fx])
        shop_rows.append({"ItemId": code, "Category": 1, "Sub1": 3, "Sub2": -1, "Field16": 0, "Field20": 0,
                          "NewFlag": 0, "Note": "0", "Field57": 0, "Name": "0"})
        en_name.append({"key": "%d.Name" % row_i, "value": name_en, "was": None,
                        "why": "23.10.2026 новые маунты донора AsdaFableClassic (Reports/fable_mounts)", "source": SOURCE})
        en_tip.append({"key": "%d.Name" % trows[-1]["_row"], "value": name_en, "was": None,
                       "why": "23.10.2026 новые маунты донора AsdaFableClassic", "source": SOURCE})
        en_tip.append({"key": "%d.Tooltip" % trows[-1]["_row"], "value": tip_en, "was": None,
                       "why": "23.10.2026 новые маунты донора AsdaFableClassic", "source": SOURCE})
        # Function_Table.txt сервера — образец строки 522 (26 токенов): скорость ток.6, цена ток.20, дни ток.25, макс. ур. ток.4
        srv_rows.append("%d\tSpecialItem_%d\t1\t1\t%d\t15\t%d\t2\t3\t0\t0\t30000\t1\t2\t1\t0\t0\t0\t2\t1\t%d\t2\t5\t-1\t1\t%d"
                        % (code, code, LEVEL_CAP.get(model, 150), speed, PRICE, days))
        summary.append([code, model, dcode, speed, days, why, name_ru, name_en, cs(d, 164, 32), d[196],
                        cs(d, 262, 32), d[295]] + fx)

    os.makedirs(TAIL_DIR, exist_ok=True)
    with open(os.path.join(TAIL_DIR, "special_itemlist.csv"), "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    with open(os.path.join(TAIL_DIR, "specialitem_tooltiplist.csv"), "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["_row"] + [c[2] for c in tcols])
        w.writeheader(); w.writerows(trows)
    with open(RIDE_TAIL, "w", encoding="utf-8", newline="") as f:
        f.write("Code,EffectName,SoundName,SoundName2,SoundName3\n")
        for r in ride_rows:
            f.write('%d,"%s","%s","%s","%s"\n' % tuple(r))
    with open(SHOP_TAIL, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"_comment": "23.10.2026: строки витрины 6000 для новых маунтов донора AsdaFableClassic "
                               "(Category 1 Инструмент, Sub1 3 Транспорт, NewFlag 0 — как у 56 прежних транспортов); строит "
                               "Tools/asda_fable_mounts_data.py, дописывает asda_import_premium_shop_bin.py",
                   "rows": shop_rows}, f, ensure_ascii=False, indent=1)
    for fn, new in (("special_itemlist.json", en_name), ("specialitem_tooltiplist.json", en_tip)):
        p = os.path.join(LOC_EN, fn)
        doc = json.load(open(p, encoding="utf-8"))
        keep = [r for r in doc["rows"] if r.get("source") != SOURCE]
        clash = {r["key"] for r in keep} & {r["key"] for r in new}
        assert not clash, "ключи EN уже заняты: %s" % sorted(clash)[:5]
        doc["rows"] = keep + new
        with open(p, "w", encoding="utf-8", newline="\r\n") as f:   # ведомости в CRLF, как были до правки
            json.dump(doc, f, ensure_ascii=False, indent=2)
            f.write("\n")
    os.makedirs(os.path.join(REPORT, "server"), exist_ok=True)
    with open(os.path.join(REPORT, "server", "Function_Table_rows.txt"), "w", encoding="cp1251", newline="\r\n") as f:
        f.write("\n".join(srv_rows) + "\n")
    with open(os.path.join(REPORT, "server", "SellItemTable_codes.txt"), "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(str(FIRST_CODE + k) for k in range(len(VARIANTS))) + "\n")
    with open(os.path.join(REPORT, "variants.csv"), "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["code", "model", "donor_code", "speed", "days", "source", "name_ru", "name_en", "atlas", "cell",
                    "sit", "height", "ride_fx", "ride_snd", "ride_snd_id2", "ride_snd_id1"])
        w.writerows(summary)
    print("вариантов %d, коды %d…%d; хвост special_itemlist _row %d…%d; подсказки _row %d…%d"
          % (len(VARIANTS), FIRST_CODE, FIRST_CODE + len(VARIANTS) - 1, rows[0]["_row"], rows[-1]["_row"],
             trows[0]["_row"], trows[-1]["_row"]))


if __name__ == "__main__":
    main()
