# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_wod_items.py — перенос 8 384 предметов донора WorldOfDreams (WoD) в клиент и на сервер порта (29.10.2026,
решение владельца: «вносим в наш клиент и сервер … полный перенос. Идентификаторы свои ставим»).
Отчёт и реестр — `Reports/donor_wod/IMPORT.md`.

ДОНОР. `Donors/WorldOfDreams` — более новая версия игры (Asda2.exe, IDB `RE/ida/global_work/Asda2_WoD.exe.i64`).
  Таблицы `Data/Character/Bin.bbz` (zip) зашифрованы XOR-ключом 256 б по позиции файла (`data/wod/key256.npy`, ключ
  восстановлен статистически 29.10). После снятия XOR: заголовок 25 б (ASCII-версия «00408» 5 б, 0, i32 count @9, …, tag «CAL»/«TII» @17),
  далее записи БЕЗ MD5-префикса и БЕЗ пофилдового шифра (в отличие от нашего архива). item_list — шаг 248 (та же раскладка
  248-байтовой записи, что DataHex нашего item_list.csv), item_img_table — шаг 300 (раскладка как у нас).
  Имена — UTF-8 (у 1 117 предметов корейские, из них ~1 100 заготовок «[P]여분자리N», «장비형 아바타_…N»).

КОДЫ. Коды WoD и наши несовместимы (под одним кодом разные предметы). Каждому недостающему предмету (статус «нет у нас»
  в `Reports/donor_wod/compare_by_icon.csv`) выдаётся СВОЙ код подряд после последнего кода порта (сервер требует
  непрерывность item_table/SowelCombine — `Reports/server_item_tail_extend.md`). Реестр соответствия —
  `data/wod/wod_item_codes.csv` (единственный источник; коды после выкладки НЕ менять).

КОМАНДЫ:
  python asda_wod_items.py registry [--first N]   — выделить коды (один раз; повтор сверяет и ничего не меняет)
  python asda_wod_items.py stats                  — сводка по реестру (типы, модели, заготовки)
  python asda_wod_items.py texts-pool             — тексты: RU из русского донора Fable (тот же код, модель, иконка, тип),
                                                    EN из WoD; пул строк на перевод → Reports/donor_wod/_work/
  python asda_wod_items.py icons [--dry]          — атласы иконок донора → RawAssets/Textures/JUI/wod_<имя>.png (только новые)
                                                    + список data/wod/wod_icon_atlases.json (мип-таблица, импорт UE)
Модуль без побочных эффектов при импорте.
"""
import argparse
import collections
import csv
import io
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))                                  # AstoryClient\Tools
sys.path.insert(0, HERE)
import asda_paths as _P                                                            # noqa: E402

PORT = _P.PORT_DIR                                                                 # @@ASTORY_DEV_ROOT@@\port
UPROJ = os.path.join(PORT, "AstoryClient")                                           # проект UE
ITEMDB_LIVE = os.path.join(UPROJ, "RawAssets", "Data", "ItemDB")                  # боевые CSV ItemDB
WOD_ROOT = os.path.join(_P.DONORS, "WorldOfDreams")                                # клиент донора
WOD_BBZ = os.path.join(WOD_ROOT, "Data", "Character", "Bin.bbz")                   # таблицы донора (zip под XOR)
DATA_DIR = os.path.join(HERE, "data", "wod")                                       # данные генератора
KEY_PATH = os.path.join(DATA_DIR, "key256.npy")                                    # XOR-ключ 256 б (numpy uint8)
REGISTRY_CSV = os.path.join(DATA_DIR, "wod_item_codes.csv")                        # реестр WoD-код → наш код
COMPARE_CSV = os.path.join(PORT, "Reports", "donor_wod", "compare_by_icon.csv")   # сравнение 29.10 (статус «нет у нас»)
MISSING_STATUS = "нет у нас"                                                       # статус недостающего предмета

WOD_HDR = 25                                                                       # заголовок таблицы донора после XOR
STRIDE = {"item_list.bin": 248, "item_img_table.bin": 300, "item_tooltip.bin": 204}                          # шаг записей донора (подтверждён)
ITEM_FIRST = 20551                                                                 # первый код (база плотных таблиц)
FABLE_BBZ = os.path.join(_P.DONORS, "AsdaFableClassic", "Data", "Character", "Bin.bbz")   # русский донор (линия кодов та же)
FABLE_KEY = os.path.join(PORT, "Reports", "fable_mounts", "_dec", "key256.npy")    # XOR-ключ Fable (другой)
WORK_DIR = os.path.join(PORT, "Reports", "donor_wod", "_work")                     # рабочие файлы перевода
TEXTS_CSV = os.path.join(DATA_DIR, "wod_item_texts.csv")                           # итоговые тексты RU/EN по нашему коду
JUI_PNG = os.path.join(UPROJ, "RawAssets", "Textures", "JUI")                     # PNG атласов JUI для импорта в UE
ICON_LIST = os.path.join(DATA_DIR, "wod_icon_atlases.json")                        # список атласов wod_* (мипы, импорт)
WOD_ICON_BTZ = [os.path.join(WOD_ROOT, "juidata", d, "Texture.btz") for d in ("sIcon", "gIcon")]  # sIcon полнее — первым
ICON_PREFIX = "wod_"                                                               # атласы донора ≠ наши одноимённые (icons.md §1)
REG_FIELDS = ["wod_code", "our_code", "name", "wod_type", "wod_sub", "mesh_male", "mesh_female",
              "icon_atlas", "icon_cell", "placeholder"]                            # колонки реестра


def out(*a):
    """Печать в UTF-8 (консоль cp1251 не ломается)."""
    s = " ".join(str(x) for x in a) + "\n"
    sys.stdout.buffer.write(s.encode("utf-8", "replace"))
    sys.stdout.flush()


def wod_file(name, bbz=WOD_BBZ, key_path=KEY_PATH):
    """Расшифрованный файл таблицы донора из Bin.bbz (XOR по позиции файла)."""
    import numpy as np
    raw = np.frombuffer(zipfile.ZipFile(bbz).read(name), np.uint8)
    key = np.load(key_path).astype(np.uint8)
    return (raw ^ np.resize(key, len(raw))).tobytes()


def wod_records(name, data=None):
    """Записи таблицы донора (список bytes) — заголовок 25 б, затем count × шаг; count проверяется по заголовку.
    item_tooltip донора: шаг 204 = i32 код + char[200] (тег «PTT»); `item_tootip.bin` — старая копия (43 181), не берём."""
    d = data if data is not None else wod_file(name)
    st = STRIDE[name]
    n = struct.unpack_from("<i", d, 9)[0]          # заголовок: 5 б ASCII-версия («00408»), 4 б 0, i32 count @9, …, тег @17
    if WOD_HDR + n * st != len(d):
        raise SystemExit("%s: размер %d ≠ 25 + %d × %d" % (name, len(d), n, st))
    return [d[WOD_HDR + i * st:WOD_HDR + (i + 1) * st] for i in range(n)]


def cstr(b, enc="utf-8"):
    """C-строка поля (UTF-8 донора; битые байты — заменой)."""
    return b.split(b"\0", 1)[0].decode(enc, "replace")


def is_placeholder(name):
    """Заготовка донора: «[P]여분자리N» (запасное место) и «장비형 아바타_…N» (безымянный аватар-шаблон)."""
    return ("여분자리" in name) or ("장비형 아바타" in name) or ("미국_이벤트용_기타아이템" in name)


def wod_items():
    """{код WoD: (запись item_list 248 б, запись item_img_table 300 б)}. Строка img = строка item_list (у донора одна img-запись
    с чужим кодом в поле +0 — 80777 на месте 80782, — поэтому соответствие строго позиционное)."""
    il = wod_records("item_list.bin")
    im = wod_records("item_img_table.bin")
    if len(il) != len(im):
        raise SystemExit("item_list %d ≠ item_img_table %d" % (len(il), len(im)))
    res = {}
    for i, (r, g) in enumerate(zip(il, im)):
        code = struct.unpack_from("<i", r, 0)[0]
        if code != ITEM_FIRST + i:
            raise SystemExit("item_list донора не плотный: строка %d код %d" % (i, code))
        res[code] = (r, g)
    return res


def fable_tables():
    """Русский донор AsdaFableClassic: {код: (item_list 248, item_img 300, tooltip 204)} — позиционно от 20551."""
    k = FABLE_KEY
    il = wod_records("item_list.bin", wod_file("item_list.bin", FABLE_BBZ, k))
    im = wod_records("item_img_table.bin", wod_file("item_img_table.bin", FABLE_BBZ, k))
    tt = wod_records("item_tooltip.bin", wod_file("item_tooltip.bin", FABLE_BBZ, k))
    return {ITEM_FIRST + i: t for i, t in enumerate(zip(il, im, tt))}


def has_cyr(s):
    """В строке есть кириллица."""
    return any("Ѐ" <= ch <= "ӿ" for ch in s)


def load_csv(path):
    """CSV → список словарей (UTF-8, BOM допустим)."""
    csv.field_size_limit(1 << 30)
    with open(path, "r", encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def load_registry():
    """Реестр кодов: список словарей по возрастанию our_code (пустой, если не выделен)."""
    if not os.path.isfile(REGISTRY_CSV):
        return []
    return sorted(load_csv(REGISTRY_CSV), key=lambda r: int(r["our_code"]))


def cmd_registry(a):
    """Выделить коды недостающим предметам: подряд с --first (по умолчанию последний код порта + 1) в порядке кодов WoD."""
    items = wod_items()
    miss = sorted(int(r["WoD_код"]) for r in load_csv(COMPARE_CSV) if r["Статус"] == MISSING_STATUS)
    port_last = max(int(r["ItemCode"]) for r in load_csv(os.path.join(ITEMDB_LIVE, "item_list.csv")))
    first = a.first or port_last + 1
    rows = []
    for k, wc in enumerate(miss):
        r, g = items[wc]
        name = cstr(r[4:68])
        rows.append({"wod_code": wc, "our_code": first + k, "name": name,
                     "wod_type": struct.unpack_from("<i", r, 128)[0], "wod_sub": struct.unpack_from("<i", r, 132)[0],
                     "mesh_male": cstr(g[4:36]), "mesh_female": cstr(g[68:100]),
                     "icon_atlas": cstr(g[132:164]), "icon_cell": struct.unpack_from("<i", g, 164)[0],
                     "placeholder": int(is_placeholder(name))})
    old = load_registry()
    if old:
        same = [(int(o["wod_code"]), int(o["our_code"])) for o in old] == [(r["wod_code"], r["our_code"]) for r in rows]
        out("реестр уже есть (%d строк): %s" % (len(old), "совпадает" if same else "РАСХОДИТСЯ — коды не перевыделяю"))
        return 0 if same else 2
    if first <= port_last:
        raise SystemExit("ОТКАЗ: первый код %d ≤ последнего кода порта %d" % (first, port_last))
    os.makedirs(DATA_DIR, exist_ok=True)
    with open(REGISTRY_CSV, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=REG_FIELDS)
        w.writeheader()
        w.writerows(rows)
    out("реестр: %d предметов, коды %d…%d → %s" % (len(rows), rows[0]["our_code"], rows[-1]["our_code"], REGISTRY_CSV))
    return 0


def cmd_stats(a):
    """Сводка реестра."""
    reg = load_registry()
    t = collections.Counter((r["wod_type"], r["wod_sub"]) for r in reg)
    out("предметов", len(reg), "заготовок", sum(int(r["placeholder"]) for r in reg),
        "с моделью", sum(1 for r in reg if r["mesh_male"] or r["mesh_female"]))
    for k, n in t.most_common():
        out("  тип/подтип WoD %s/%s: %d" % (k[0], k[1], n))
    return 0


def icon_atlases():
    """{имя атласа донора (нижний регистр): число предметов реестра} — мужское (@132) и женское (@172) поля."""
    items = wod_items()
    need = collections.Counter()
    for r in load_registry():
        g = items[int(r["wod_code"])][1]
        for off in (132, 172):
            nm = cstr(g[off:off + 32]).lower()
            if nm:
                need[nm] += 1
    return need


TEXT_FIELDS = ["our_code", "wod_code", "name_en", "tip_en", "name_ru", "tip_ru", "ru_src", "en_src"]  # колонки текстов


def cmd_texts_pool(a):
    """Тексты переноса. RU: русский донор Fable, если у него ТОТ ЖЕ код с той же моделью (@4), иконкой (@132…@167) и тем же
    типом/подтипом (@128/@132) и имя кириллическое (иначе под кодом лежит другой предмет — проверено: бустер на месте брюк).
    EN: имя/подсказка WoD; корейские (UTF-8) — в пул перевода на EN. Остальное — в пул перевода на RU (`_work/tr_*.tsv`)."""
    items = wod_items()
    tips = wod_records("item_tooltip.bin")
    fab = fable_tables()
    reg = load_registry()
    rows, need_ru_name, need_ru_tip, need_en = [], collections.Counter(), collections.Counter(), collections.Counter()
    style = []
    for r in reg:
        wc = int(r["wod_code"])
        rec, img = items[wc]
        name_en = cstr(rec[4:68])
        tip_en = cstr(tips[wc - ITEM_FIRST][4:204])
        if struct.unpack_from("<i", tips[wc - ITEM_FIRST], 0)[0] != wc:
            raise SystemExit("подсказка донора не позиционна: %d" % wc)
        name_ru = tip_ru = ""
        f = fab.get(wc)
        if f and f[1][4:36] == img[4:36] and f[1][132:168] == img[132:168] and f[0][128:136] == rec[128:136]:
            n = cstr(f[0][4:68], "cp1251").strip()
            t = cstr(f[2][4:204], "cp1251").strip()
            if has_cyr(n):
                name_ru = n
                style.append((name_en, n))
            if has_cyr(t) and t.lower() not in ("нет",):
                tip_ru = t
        en_src = "wod"
        for s_ in (name_en, tip_en):
            if any(ord(ch) > 0x7f for ch in s_) and any("가" <= ch <= "힣" for ch in s_):
                need_en[s_] += 1
                en_src = "translate"
        if not name_ru:
            need_ru_name[name_en] += 1
        if not tip_ru and tip_en:
            need_ru_tip[tip_en] += 1
        rows.append({"our_code": r["our_code"], "wod_code": wc, "name_en": name_en, "tip_en": tip_en, "name_ru": name_ru,
                     "tip_ru": tip_ru, "ru_src": "fable" if name_ru else "", "en_src": en_src})
    os.makedirs(WORK_DIR, exist_ok=True)
    for fn, pool in (("tr_names_ru.tsv", need_ru_name), ("tr_tips_ru.tsv", need_ru_tip), ("tr_korean_en.tsv", need_en)):
        with open(os.path.join(WORK_DIR, fn), "w", encoding="utf-8", newline="\n") as f:
            f.write("count\tsource\ttranslation\n")
            for s_, n in sorted(pool.items(), key=lambda x: (-x[1], x[0])):
                f.write("%d\t%s\t\n" % (n, s_.replace("\t", " ").replace("\n", " ")))
    with open(os.path.join(WORK_DIR, "style_fable_en_ru.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("en\tru\n")
        for e, r_ in sorted(set(style)):
            f.write("%s\t%s\n" % (e, r_))
    with open(os.path.join(WORK_DIR, "texts_base.csv"), "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=TEXT_FIELDS)
        w.writeheader()
        w.writerows(rows)
    out("тексты: RU из Fable имён %d / подсказок %d; на перевод RU: имён %d, подсказок %d; корейских → EN %d"
        % (sum(1 for x in rows if x["name_ru"]), sum(1 for x in rows if x["tip_ru"]), len(need_ru_name), len(need_ru_tip),
           len(need_en)))
    return 0


# Единые термины перевода (переводчики работали параллельно): имена собственные — как в именах предметов (Fable/глоссарий
# порта); заготовки донора — одной формулой; Sowel = «живой камень» (термин порта). Правило: (регэксп, замена).
RU_NORMALIZE = [
    (r"(?<![А-Яа-яЁё])Тили(я|и|ю|ей|е)(?![А-Яа-яЁё])", r"Тилли\1"),
    (r"(?<![А-Яа-яЁё])Атер(а|у|ом|е)?(?![А-Яа-яЁё])", r"Артер\1"),
    (r"Резервный слот (\d+)", r"Запасное место \1"),
    (r"Аватар-снаряжение: верх", "Аватар-снаряжение: доспех"),
    (r"Аватар-снаряжение: низ", "Аватар-снаряжение: штаны"),
    (r"Аватар-снаряжение: обувь", "Аватар-снаряжение: ботинки"),
    (r"средство для удаления соула", "набор для удаления живого камня"),
]


def ru_norm(s):
    """Нормализация русского текста по RU_NORMALIZE."""
    import re
    for pat, rep in RU_NORMALIZE:
        s = re.sub(pat, rep, s)
    return s


def _read_done(fn):
    """{source: translation} из `_work/<fn>` (TSV count/source/translation)."""
    res = {}
    with open(os.path.join(WORK_DIR, fn), encoding="utf-8") as f:
        next(f)
        for ln in f:
            parts = ln.rstrip("\n").split("\t")
            if len(parts) != 3:
                raise SystemExit("%s: строка не из 3 колонок: %r" % (fn, ln[:80]))
            res[parts[1]] = parts[2]
    return res


def cmd_texts_build(a):
    """Итоговые тексты `data/wod/wod_item_texts.csv`: RU = Fable или перевод (нормализованный), EN = WoD или перевод корейского.
    Проверки: RU-имя ≤ 63 байт cp1251, RU-подсказка ≤ 199 (поля char[64]/char[200] item_list/item_tooltip), всё кодируется cp1251."""
    base = load_csv(os.path.join(WORK_DIR, "texts_base.csv"))
    names = {**_read_done("tr_names_ru_part1.done.tsv"), **_read_done("tr_names_ru_part2.done.tsv")}
    tips = {**_read_done("tr_tips_ru_part1.done.tsv"), **_read_done("tr_tips_ru_part2.done.tsv")}
    ko = _read_done("tr_korean_en.done.tsv")
    errs, rows = [], []
    for r in base:
        name_ru = r["name_ru"] or names.get(r["name_en"], "")
        tip_ru = r["tip_ru"] or (tips.get(r["tip_en"], "") if r["tip_en"] else "")
        name_ru, tip_ru = ru_norm(name_ru).strip(), ru_norm(tip_ru).strip()
        if len(tip_ru.encode("cp1251", "replace")) > 199:   # у Fable 3 подсказки обрезаны полем 200 б на полуслове
            tip_ru = tip_ru[:196].rsplit(" ", 1)[0].rstrip(",;: ") + "..."
        name_en = ko.get(r["name_en"], r["name_en"])
        tip_en = ko.get(r["tip_en"], r["tip_en"])
        for what, s, lim in (("имя", name_ru, 63), ("подсказка", tip_ru, 199)):
            try:
                n = len(s.encode("cp1251"))
            except UnicodeEncodeError as e:
                errs.append("%s %s: не cp1251 %r" % (r["our_code"], what, s[e.start:e.end]))
                continue
            if n > lim:
                errs.append("%s %s: %d > %d" % (r["our_code"], what, n, lim))
        if not name_ru:
            errs.append("%s: нет RU-имени (%s)" % (r["our_code"], r["name_en"]))
        if any("가" <= ch <= "힣" for ch in name_en + tip_en):
            errs.append("%s: корейский в EN" % r["our_code"])
        rows.append({**r, "name_ru": name_ru, "tip_ru": tip_ru, "name_en": name_en, "tip_en": tip_en,
                     "ru_src": r["ru_src"] or "translate", "en_src": r["en_src"]})
    if errs:
        for e in errs[:30]:
            out("  !!", e)
        raise SystemExit("ОТКАЗ: ошибок текстов %d" % len(errs))
    with open(TEXTS_CSV, "w", encoding="utf-8", newline="") as f:
        w = csv.DictWriter(f, fieldnames=TEXT_FIELDS)
        w.writeheader()
        w.writerows(rows)
    out("тексты: %d предметов → %s (RU из Fable %d, перевод %d)" % (len(rows), TEXTS_CSV,
        sum(1 for x in rows if x["ru_src"] == "fable"), sum(1 for x in rows if x["ru_src"] == "translate")))
    return 0


def cmd_icons(a):
    """Атласы иконок донора → PNG `wod_<имя>` (наши одноимённые атласы — другое содержимое, их не трогаем)."""
    import json
    import asda_extract_archive_textures as X
    need = icon_atlases()
    zips = [zipfile.ZipFile(p) for p in WOD_ICON_BTZ]
    index = [{os.path.basename(n).lower(): n for n in z.namelist()} for z in zips]
    made, kept, missing, listed = 0, 0, [], []
    for nm in sorted(need):
        src = next(((z, ix[nm + ".tex"]) for z, ix in zip(zips, index) if nm + ".tex" in ix), None)
        if not src:
            missing.append(nm)
            continue
        listed.append(ICON_PREFIX + nm)
        dst = os.path.join(JUI_PNG, ICON_PREFIX + nm + ".png")
        if os.path.exists(dst):
            kept += 1
            continue
        if a.dry:
            made += 1
            continue
        img = X.to_rgba(src[0].read(src[1])[X.HDR:])
        img.save(dst)
        made += 1
    if not a.dry:
        with open(ICON_LIST, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"_comment": "Атласы иконок донора WorldOfDreams, заведённые как wod_<имя> (asda_wod_items.py icons). "
                                   "Источник juidata\\sIcon (иначе gIcon) Texture.btz; импорт NO_MIPMAPS.", "atlases": listed},
                      f, ensure_ascii=False, indent=1)
            f.write("\n")
    out("атласов нужно %d: новых PNG %d, уже было %d, нет у донора %d %s" % (len(need), made, kept, len(missing), missing[:10]))
    return 1 if missing else 0


WOD_MODEL_BNZ = os.path.join(WOD_ROOT, "Data", "Character", "Model", "CharModel.bnz")       # модели донора
WOD_TEX_BTZ = os.path.join(WOD_ROOT, "Data", "Character", "Texture", "Texture.btz")        # текстуры донора
OUR_MODEL_BNZ = os.path.join(_P.DATA, "Character", "Model", "CharModel.bnz")               # наши модели (MINE)
OUR_TEX_BTZ = os.path.join(_P.DATA, "Character", "Texture", "Texture.btz")                 # наши текстуры (MINE)
MODELS_JSON = os.path.join(DATA_DIR, "wod_models.json")                                    # план моделей/текстур
EQUIP_GLB = os.path.join(UPROJ, "RawAssets", "Models", "Skeletal", "EquipParts")           # .glb скиннутых частей
CHAR_GLB = os.path.join(UPROJ, "RawAssets", "Models", "Char")                              # .glb статик-мешей
CHAR_TEX_PNG = os.path.join(UPROJ, "RawAssets", "Textures", "Data", "Character", "Texture")  # PNG текстур персонажа
PATCH_SIG = b"tcbCLFwT"                     # сигнатура доп. шифра донора (серии 501/502: avatar, acce501, mantle_501/502)
PATCH_KEY = b"$\x02\x16 "                   # XOR 4 б по позиции; после него 32 б «PatchDataMaker_2016.02.23_v1.00\0»
SKINNED = __import__("re").compile(r"^(ch1_[mf]_\d+_(chst|pant|shoe|grov|hcap)\d|avatar_|acce|mantle|rabbit)")


def unpatch(raw):
    """Снять доп. шифр донора (серии 501/502), если он есть."""
    if not raw.startswith(PATCH_SIG):
        return raw
    k = (PATCH_KEY * (len(raw) // 4 + 1))[:len(raw)]
    return bytes(a ^ b for a, b in zip(raw, k))[32:]


def _zindex(path):
    """(zip, {базовое имя в нижнем регистре: имя в архиве})."""
    z = zipfile.ZipFile(path)
    return z, {os.path.basename(n).lower(): n for n in z.namelist()}


def mesh_gender(name):
    """Пол скиннутой части по имени: m / f (mantle/rabbit — явно по «_man»/«_woman»)."""
    n = name.lower()
    if "_woman" in n or "_f_" in n:
        return "f"
    if "_man" in n or "_m_" in n:
        return "m"
    return "n"


def cmd_models_plan(a):
    """План моделей и текстур: для каждого меша/текстуры предметов реестра — new (нет у нас: импорт под тем же именем),
    same (у нас побайтно то же / та же геометрия: берём наш ассет), wod (у нас одноимённый с ДРУГОЙ геометрией/картинкой:
    импорт донорского под `wod_<имя>`, ссылки в img переписываются), no_donor (у донора файла нет — как у донора, пусто)."""
    import json
    import numpy as np
    from asda_jbm_to_gltf import parse_jbm_bytes
    items = wod_items()
    wm, WM = _zindex(WOD_MODEL_BNZ)
    om, OM = _zindex(OUR_MODEL_BNZ)
    wt, WT = _zindex(WOD_TEX_BTZ)
    ot, OT = _zindex(OUR_TEX_BTZ)
    meshes, texs = collections.defaultdict(set), collections.defaultdict(set)
    for r in load_registry():
        g = items[int(r["wod_code"])][1]
        for mo, to in ((4, 36), (68, 100)):
            m, t = cstr(g[mo:mo + 32]).lower(), cstr(g[to:to + 32]).lower()
            if m:
                meshes[m].add(int(r["our_code"]))
            if t:
                texs[t].add(int(r["our_code"]))

    def geo(raw):
        return [np.asarray(me.get("pos"), dtype=float) for me in parse_jbm_bytes(raw)["meshes"]]

    plan_m, plan_t = [], []
    for m in sorted(meshes):
        w, o = WM.get(m + ".jbm"), OM.get(m + ".jbm")
        kind = "skin" if SKINNED.match(m) else "static"
        if not w:
            act = "no_donor"
        elif not o:
            act = "new"
        else:
            ra, rb = unpatch(wm.read(w)), om.read(o)
            if ra == rb:
                act = "same"
            else:
                ga, gb = geo(ra), geo(rb)
                same = len(ga) == len(gb) and all(x.shape == y.shape and (x.size == 0 or np.abs(x - y).max() < 1e-3)
                                                 for x, y in zip(ga, gb))
                act = "same" if same else "wod"
        out_name = ICON_PREFIX + m if act == "wod" else m
        if len(out_name) > 31:
            raise SystemExit("имя меша %s длиннее char[32]" % out_name)
        plan_m.append({"name": m, "out": out_name, "kind": kind, "gender": mesh_gender(m) if kind == "skin" else "n",
                       "action": act, "src": w or "", "items": len(meshes[m])})
    for t in sorted(texs):
        w, o = WT.get(t + ".tex"), OT.get(t + ".tex")
        if not w:
            act = "no_donor"
        elif not o:
            act = "new"
        else:
            act = "same" if unpatch(wt.read(w)) == ot.read(o) else "wod"
        out_name = ICON_PREFIX + t if act == "wod" else t
        if len(out_name) > 31:
            raise SystemExit("имя текстуры %s длиннее char[32]" % out_name)
        plan_t.append({"name": t, "out": out_name, "action": act, "src": w or "", "items": len(texs[t])})
    with open(MODELS_JSON, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"_comment": "План моделей/текстур предметов WoD (asda_wod_items.py models-plan). action: new — импорт под тем "
                               "же именем; same — наш ассет; wod — одноимённый у нас другой, донорский под out=wod_<имя>; "
                               "no_donor — файла у донора нет.", "meshes": plan_m, "textures": plan_t},
                  f, ensure_ascii=False, indent=1)
        f.write("\n")
    for nm, pl in (("меши", plan_m), ("текстуры", plan_t)):
        c = collections.Counter((p.get("kind", ""), p["action"]) for p in pl)
        out(nm, dict(sorted(c.items())))
    return 0


def cmd_models_export(a):
    """Меши плана (new/wod) → .glb: скиннутые — штатным движком частей (`export_equip_parts._export_part_inner`, общий скелет
    cre_cremmy), статик — `asda_build_models.convert_one` (категория Char); текстуры — PNG (декодер `.tex` порта).
    Существующие .glb/PNG не перезаписываются."""
    import json
    import export_equip_parts as EP
    import asda_build_models as BM
    import asda_skinned_to_gltf as S
    import asda_extract_archive_textures as X
    import asda_alpha_bleed as AB   # ★ 07.11.2026 (summer-hem): заполнение цвета под прозрачностью
    plan = json.load(open(MODELS_JSON, encoding="utf-8"))
    wm, WM = _zindex(WOD_MODEL_BNZ)
    mot = zipfile.ZipFile(os.path.join(WOD_ROOT, "Data", "Character", "Motion", "CharMotion.baz"))
    ref = S.parse_jbb(mot.read(next(n for n in mot.namelist() if n.lower().endswith("cre_cremmy-a_00_0.jbb"))))
    wt, _ = _zindex(WOD_TEX_BTZ)
    stat = collections.Counter()
    bad = []
    for p in plan["meshes"]:
        if p["action"] not in ("new", "wod"):
            continue
        if a.only and not __import__("re").search(a.only, p["out"]):
            continue
        raw = unpatch(wm.read(p["src"]))
        if p["kind"] == "skin":
            dst = os.path.join(EQUIP_GLB, p["out"] + ".glb")
            if os.path.exists(dst):
                stat["skin_есть"] += 1
                continue
            ok, res = EP._export_part_inner(p["out"], raw, ref, verbose=False)
            stat["skin_ok" if ok else "skin_ПРОВЕРЬ"] += 1
            if not ok:
                bad.append((p["out"], res))
        else:
            dst = os.path.join(CHAR_GLB, p["out"] + ".glb")
            if os.path.exists(dst):
                stat["static_есть"] += 1
                continue
            ok, res = BM.convert_one(p["out"] + ".jbm", raw, CHAR_GLB, "Char")
            stat["static_ok" if ok else "static_FAIL"] += 1
            if not ok:
                bad.append((p["out"], res))
    for p in plan["textures"]:
        if p["action"] not in ("new", "wod"):
            continue
        dst = os.path.join(CHAR_TEX_PNG, p["out"] + ".png")
        if os.path.exists(dst):
            stat["tex_есть"] += 1
            continue
        try:
            X.to_rgba(unpatch(wt.read(p["src"]))[X.HDR:]).save(dst)
            AB.bleed_file(dst)   # ★ 07.11.2026 [ИСПРАВЛЕНИЕ] summer-hem: цвет под прозрачностью у текстур таблицы прозрачных частей
            stat["tex_ok"] += 1
        except Exception as e:   # битый .tex донора — в отчёт, не молча
            stat["tex_FAIL"] += 1
            bad.append((p["out"], str(e)[:60]))
    out("экспорт:", dict(stat))
    for b in bad[:40]:
        out("  !!", b)
    return 1 if bad else 0


def main(argv=None):
    """Точка входа."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("registry")
    p.add_argument("--first", type=int, default=0, help="первый код (по умолчанию последний код порта + 1)")
    sub.add_parser("stats")
    sub.add_parser("texts-pool")
    sub.add_parser("texts-build")
    sub.add_parser("models-plan")
    p = sub.add_parser("models-export")
    p.add_argument("--only", default="", help="регэксп по имени (проба)")
    p = sub.add_parser("icons")
    p.add_argument("--dry", action="store_true", help="только посчитать")
    a = ap.parse_args(argv)
    return {"registry": cmd_registry, "stats": cmd_stats, "icons": cmd_icons, "texts-pool": cmd_texts_pool, "texts-build": cmd_texts_build,
            "models-plan": cmd_models_plan, "models-export": cmd_models_export}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
