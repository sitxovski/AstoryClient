# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_build_name_overlay.py — ГЕНЕРАТОР слоя-рецепта имён/описаний предметов.

ЗАЧЕМ
  В русском клиенте (AsdaChroniclesInterlude_MINE) строковые поля item-таблиц частично
  утрачены («-»), частично техномусор («_», «252», «???»), а у части записей текст принадлежит
  ДРУГОЙ записи (доказано сверкой с числовым блоком той же записи — см.
  Reports/item_names_overlay.md). Числовые данные RU-клиента при этом побайтово равны
  EN-клиенту, то есть портится только текст.

  Декодер Tools/decode_item_db.py обязан оставаться 1:1 с клиентом. Поэтому исправления
  живут ОТДЕЛЬНЫМ файлом-рецептом, а применяются скриптом asda_apply_name_overlay.py
  прямо в игровые CSV (перманентно, без рантайм-подмен).

ЧТО НА ВХОДЕ (только чтение)
  RU  @@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\Character\Bin.bbz   — наша база (cp1251)
  EN  @@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude\Data\Character\Bin.bbz        — донор (cp1252/cp949)
  AR  @@ASTORY_DEV_ROOT@@\Donors\ArabicASDAStory\data\Character\Bin.bbz              — донор (cp1256)
  SRV <живой v2>\GameServer\ItemTable_Info\ItemName_Table.txt           — донор (cp1256); путь — asda_server_txt.LIVE_V2,
      без доступа к серверу — локальный снимок @@ASTORY_DEV_ROOT@@\Donors\GameServer_stock\... (устарел: 52 253 записи против 58 878)
  SRV Function_Table.txt — НЕ читается: классы функций спецпредметов вписаны в FUNC_CLASS (колонка 5 → поле @80)
  LEG port\...\ItemDB\item_names_recovered.csv                          — прежний слой (не терять)

ЧТО НА ВЫХОДЕ
  AstoryClient\RawAssets\Data\ItemDB\item_name_overlay.csv
      table,key,column,old_value,new_value,source,reason,note
  Формат нарочно «плоский»: одна строка = одна правка одной ячейки. Правится руками,
  сортирован детерминированно (table,key,column) — дифф между прогонами читаемый.

ПРИОРИТЕТ ИСТОЧНИКОВ (обоснование в отчёте, §«Приоритет»)
  en     EN-клиент того же билда — числовой блок записи побайтово равен нашему => имя точно от
         этой записи. Сильнейший источник.
  ko     тот же файл EN-клиента, но строка в cp949 (корейский оригинал разработчика).
         Берётся, только если cp1252 даёт мохибейк.
  ar     арабский клиент — другой билд, кодовое пространство пересекается частично.
  srv    ItemName_Table.txt игрового сервера (арабский cp1256, пробел = «_»). ★ 11.10.2026: АНГЛИЙСКОЕ имя сервера
         (чистый ASCII) берётся раньше ar — правило владельца 27.09 «RU>EN>AR».
  legacy прежний item_names_recovered.csv — если он знает имя, которого нет выше.

ПРИЧИНЫ (reason)
  ru_missing   RU-значение — плейсхолдер («-», пусто, «Нет», «NONE», «0»)      [задача 1]
  ru_junk      RU-значение — техномусор: только цифры / «_» / мохибейк «???»   [задача 2]
  ru_mismatch  RU-текст противоречит числовому блоку СВОЕЙ записи, а EN-текст
               ему соответствует — текст принадлежит другой записи            [задача 2]

Запуск:  python AstoryClient\Tools\asda_build_name_overlay.py
         python ... --stats-only      (ничего не писать, только числа)
"""
import argparse
import collections
import csv
import io
import os
import re
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_paths as _P                             # noqa: E402  ★ дедуп №2 В1 Т20: корень диска (ASDA_ROOT)
ROOT     = _P.ROOT
PROJECT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEMDB   = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB')
# ⚠ Не в `Content/` — иначе движок покажет рецепт как игровой ассет (см. asda_apply_name_overlay.py).
OUT_CSV  = os.path.join(PROJECT, 'Tools', '_out', 'itemdb_overlay', 'item_name_overlay.csv')
LEGACY   = os.path.join(ITEMDB, 'item_names_recovered.csv')

RU_BBZ = os.path.join(ROOT, 'Donors', 'AsdaChroniclesInterlude_MINE', 'Data', 'Character', 'Bin.bbz')
EN_BBZ = os.path.join(ROOT, 'Donors', 'AsdaChroniclesInterlude', 'Data', 'Character', 'Bin.bbz')
AR_BBZ = os.path.join(ROOT, 'Donors', 'ArabicASDAStory', 'data', 'Character', 'Bin.bbz')
# ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-доделки, 11.10.2026, решение владельца): донор имён сервера — ЖИВОЙ v2
#   (`asda_server_txt.LIVE_V2`, закон server-live-path). Локальная копия `@@ASTORY_DEV_ROOT@@\Donors\GameServer_stock` устарела
#   (52 253 записи против 58 878 на живом) и остаётся только запасным путём, если сервер недоступен.
import asda_server_txt as _ST                       # noqa: E402  — один путь к живому серверу и чтение .txt как у сервера
SRV_NAMES = _ST.live_itemtable('ItemName_Table.txt')                                          # донор: живой сервер
SRV_ITEMTABLE = _ST.live_itemtable('Item_Table.txt')                                           # число записей донора
SRV_NAMES_SNAPSHOT = os.path.join(ROOT, 'Donors', 'GameServer_stock', 'ItemTable_Info', 'ItemName_Table.txt')  # запасной локальный снимок
SRV_ITEMTABLE_SNAPSHOT = os.path.join(ROOT, 'Donors', 'GameServer_stock', 'ItemTable_Info', 'item_table.txt')  # его Item_Table

OVERLAY_HEADER = ['table', 'key', 'column', 'old_value', 'new_value', 'source', 'reason', 'note']

# ---------------------------------------------------------------- дешифровка (1:1 с decode_item_db)
# ★ Дедуп №2, В1 Т5 (11.10.2026): своя копия sub_dw/sub_w/sub_b/dec_str убрана — берём ТЕ ЖЕ функции
#   декодера `Tools/decode_item_db.py` (модуль импортируется, как в asda_title_roundtrip.py); тело dec_str
#   там — `asda_bintable.decipher_str_inplace` (TERM_AUTO, закон item-string-cipher-terminator).
#   Импорт декодера без побочных эффектов (main() под `if __name__`).
_DECODER_DIR = os.path.join(ROOT, "AstoryClient", "Tools", "ItemDB")          # папка декодера ItemDB (на уровень выше порта)
if _DECODER_DIR not in sys.path:
    sys.path.insert(0, _DECODER_DIR)
import decode_item_db as _DEC                       # noqa: E402  — общий декодер ItemDB (он же подключает asda_bintable)
import asda_bintable as _BT                         # noqa: E402  — общий разбор C-строк

sub_dw = _DEC.sub_dw        # u32-поле − константа (по модулю 2^32), на месте
sub_w = _DEC.sub_w          # u16-поле − константа, на месте
sub_b = _DEC.sub_b          # u8-поле − константа, на месте
dec_str = _DEC.dec_str      # снятие шифра строки «байт−1» на месте (TERM_AUTO)

IL_DW = {0, 17, 19, 20, 21, 22, 23, 24, 25, 27, 28, 30, 31, 32, 33, 35, 36, 37, 38, 39,
         40, 41, 42, 43, 45, 46, 48, 49, 50, 51, 53, 57, 58, 54, 56, 59}
IL_W = {36, 52, 58, 68, 69, 88, 94, 104, 110}


def _entry(bbz, name):
    z = zipfile.ZipFile(bbz)
    return z.read({n.lower(): n for n in z.namelist()}[name])


def _raw(b, off, ln):
    """сырые байты строки до 0x00 (общий `asda_bintable.cstr_bytes`)"""
    return bytes(_BT.cstr_bytes(b, off, ln))


def _txt(raw, enc, try_ko=False):
    """Декод строки.

    try_ko разрешается ТОЛЬКО для EN-клиента: там часть строк осталась корейской (cp949).
    Для RU/AR попытка cp949 недопустима — кириллица cp1251 тоже раскодируется в cp949
    и даёт хангыль, то есть русское имя превратилось бы в корейский мусор."""
    if not raw:
        return '', 'ascii'
    if all(c < 0x80 for c in raw):
        return raw.decode('ascii', 'replace').strip(), 'ascii'
    if try_ko:
        try:
            ko = raw.decode('cp949')
            if any(0xAC00 <= ord(ch) <= 0xD7A3 for ch in ko):
                return ko.strip(), 'ko'
        except Exception:
            pass
        # ★★ 2026-09-27 (itemsfix, СРЕДН-5 ревизии items_consistency). Строка EN-клиента без хангыля,
        #   но в cp949: типографика ряда 0xA1 (’ “ ” … — в cp1252 выглядит как «¡¯ ¡° ¡¦»), японская
        #   катакана ряда 0xAB (71399 «ブル?デ?モンヘルム», в cp1252 — «Ö«ë?…»), полноширинные знаки
        #   ряда 0xA3 («30％», в cp1252 — «30£¥»). Раньше такие строки уходили в cp1252 и давали
        #   кракозябры; 27.09 725 + 190 строк рецепта перекодировали вручную — это правило делает то же
        #   самое при пересборке, чтобы она не вернула мусор. Поле, обрезанное на середине двухбайтового
        #   знака, декодируется без последнего неполного байта (как в ручной перекодировке).
        #   Источник остаётся 'en' (вид 'enc949'): это тот же донор, другая только кодировка.
        #   (Хвостовой пробел, открывшийся после отброса байта, снимает add(); 27.09 у 19 подсказок
        #   52340, 56052… он был и в рецепте, и в таблице — выровнено той же волной.)
        ko = None
        rb = raw.strip()
        try:
            ko = rb.decode('cp949')
        except UnicodeDecodeError as e:
            if e.start >= len(rb) - 1:
                try:
                    ko = rb[:-1].decode('cp949')
                except UnicodeDecodeError:
                    ko = None
        if ko is not None and any(ord(ch) >= 0x2000 for ch in ko):
            return ko, 'enc949'
    return raw.decode(enc, 'replace').strip(), 'enc'


def i32(b, off):
    return struct.unpack_from('<i', b, off)[0]


def u16(b, off):
    return struct.unpack_from('<H', b, off)[0]


def load_item_list(bbz, enc, try_ko=False):
    d = _entry(bbz, 'item_list.bin')
    cnt = struct.unpack_from('<i', d, 4)[0]
    out = {}
    for i in range(cnt):
        off = 20 + i * 264 + 16
        b = bytearray(d[off:off + 248])
        for x in IL_DW:
            sub_dw(b, x * 4, 35)
        for x in IL_W:
            sub_w(b, x * 2, 35)
        dec_str(b, 4, 64)
        t, k = _txt(_raw(b, 4, 64), enc, try_ko)
        out[i32(b, 0)] = dict(name=t, kind=k)
    return out


def load_item_tooltip(bbz, enc, try_ko=False):
    d = _entry(bbz, 'item_tooltip.bin')
    cnt = struct.unpack_from('<i', d, 4)[0]
    out = {}
    for i in range(cnt):
        off = 20 + i * 220 + 16
        b = bytearray(d[off:off + 204])
        sub_dw(b, 0, 30)
        dec_str(b, 4, 164)
        t, k = _txt(_raw(b, 4, 164), enc, try_ko)
        out[i32(b, 0)] = dict(text=t, kind=k)
    return out


def load_sowell_list(bbz, enc, try_ko=False):
    d = _entry(bbz, 'sowell_list.bin')
    cnt = struct.unpack_from('<i', d, 4)[0]
    out = {}
    for i in range(cnt):
        off = 20 + i * 188 + 16
        b = bytearray(d[off:off + 172])
        sub_dw(b, 0, 16)
        dec_str(b, 4, 32)
        t, k = _txt(_raw(b, 4, 32), enc, try_ko)
        out[i32(b, 0)] = dict(name=t, kind=k)
    return out


def load_sowell_tooltip(bbz, enc, try_ko=False):
    d = _entry(bbz, 'sowell_tooltip.bin')
    cnt = struct.unpack_from('<i', d, 4)[0]
    out = {}
    for i in range(cnt):
        off = 20 + i * 220 + 16
        b = bytearray(d[off:off + 204])
        sub_dw(b, 0, 33)
        dec_str(b, 4, 200)
        t, k = _txt(_raw(b, 4, 200), enc, try_ko)
        out[i32(b, 0)] = dict(text=t, kind=k)
    return out


def load_special(bbz, enc, try_ko=False):
    d = _entry(bbz, 'special_itemlist.bin')
    cnt = struct.unpack_from('<i', d, 4)[0]
    out = {}
    for i in range(cnt):
        off = 20 + i * 316 + 16
        b = bytearray(d[off:off + 300])
        sub_dw(b, 0, 32); sub_w(b, 68, 32); sub_dw(b, 72, 32); sub_dw(b, 76, 32); sub_dw(b, 80, 32)
        sub_w(b, 84, 32); sub_b(b, 86, 32); sub_b(b, 87, 32)
        sub_w(b, 88, 33); sub_dw(b, 92, 33); sub_dw(b, 96, 33)
        for o in range(100, 107):
            sub_b(b, o, 33)
        sub_dw(b, 108, 33); sub_dw(b, 112, 33); sub_b(b, 116, 33); sub_b(b, 117, 33)
        sub_dw(b, 120, 33); sub_b(b, 124, 33); sub_dw(b, 128, 33)
        sub_b(b, 196, 34); sub_b(b, 229, 34)
        sub_b(b, 294, 33); sub_b(b, 295, 33); sub_b(b, 296, 33)
        dec_str(b, 4, 64)                       # Name — char[64] @4 (числовой блок с @68)
        for o in (132, 164, 197, 230, 262):
            dec_str(b, o, 32)
        t, k = _txt(_raw(b, 4, 64), enc, try_ko)
        out[i32(b, 0)] = dict(name=t, kind=k, sub=i32(b, 80), val=u16(b, 84),
                              qty=u16(b, 88), dur=i32(b, 92), cd=i32(b, 96))
    return out


def load_special_tooltip(bbz, enc, try_ko=False):
    """id первой записи клиент принудительно переписывает единицей (item_inventory.c:63721)."""
    d = _entry(bbz, 'specialitem_tooltiplist.bin')
    cnt = struct.unpack_from('<i', d, 4)[0]
    out = {}
    for i in range(cnt):
        off = 20 + i * 252 + 16
        b = bytearray(d[off:off + 236])
        sub_dw(b, 0, 36)
        dec_str(b, 4, 32)
        dec_str(b, 36, 180)
        if i == 0:
            struct.pack_into('<I', b, 0, 1)
        n, kn = _txt(_raw(b, 4, 32), enc, try_ko)
        t, kt = _txt(_raw(b, 36, 180), enc, try_ko)
        out[i32(b, 0)] = dict(name=n, text=t, kind=kn if kn != 'ascii' else kt)
    return out


def load_server_names():
    """ItemName_Table.txt: 'count' затем пары «id имя»; пробел закодирован как «_» (cp1256).

    ★ ИСПРАВЛЕНИЕ (11.10.2026): читается так, как читает сервер (`GameServer.c:68325-68354`): первое слово
      (`fscanf("%d")`) сервер отбрасывает, затем ровно N пар `fscanf("%s")`+`atol` / `fscanf("%s")`, где N — число
      записей Item_Table.txt (а не заголовок самого файла). Источник — живой v2; без доступа — локальный снимок
      (печатается, какой взят). Прежний разбор по строкам/табам на живом файле дал тот же словарь (0 расхождений)."""
    out = {}
    path, kind = _ST.live_or_snapshot(SRV_NAMES, SRV_NAMES_SNAPSHOT)
    item_table = SRV_ITEMTABLE if kind == 'live' else SRV_ITEMTABLE_SNAPSHOT
    if not os.path.isfile(path) or not os.path.isfile(item_table):
        print('  ! донор имён сервера недоступен: %s' % path)
        return out
    n = _ST.TokenStream(tokens=_ST.read_tokens(item_table)[:1]).count()   # число записей Item_Table (первое слово)
    ts = _ST.TokenStream(path)
    ts.count()                                     # заголовок ItemName_Table сервер читает и не использует
    for _ in range(max(0, n)):
        code = ts.i32()
        name = ts.word()
        if ts.short:
            break
        out[code] = name.decode('cp1256', 'replace').replace('_', ' ').strip()
    print('  донор имён сервера: %s (%s), записей %d%s' % (path, kind, len(out),
          ('; усечённые числа: ' + ts.lossy_text()) if ts.lossy else ''))
    return out


# ---------------------------------------------------------------- классификация значений

PLACEHOLDERS = {'', '-', '-~', 'нет', 'none', '0', '_'}
RE_DIGITS = re.compile(r'^[\d\s]+$')
RE_MOJI = re.compile(r'^[?\s_\-().,0-9]+$')


def _is_cjk(ch):
    o = ord(ch)
    return 0x3000 <= o <= 0x9FFF or 0xAC00 <= o <= 0xD7A3 or 0xF900 <= o <= 0xFAFF


def classify(value, cjk_is_junk=True):
    """'' -> ok | 'missing' (плейсхолдер) | 'junk' (техномусор/непереведённый CJK).

    cjk_is_junk=True  — оценка ЗНАЧЕНИЯ В RU-ЛОКАЛИ: корейская строка = «имени нет».
    cjk_is_junk=False — оценка ДОНОРА: корейское имя разработчика лучше, чем ничего."""
    t = (value or '').strip()
    if t.lower() in PLACEHOLDERS:
        return 'missing'
    if RE_DIGITS.match(t) or RE_MOJI.match(t):
        return 'junk'
    if cjk_is_junk:
        letters = [c for c in t if c.isalpha()]
        if letters and sum(1 for c in letters if _is_cjk(c)) * 2 > len(letters):
            return 'junk'
    return 'ok'


def usable(value):
    """годится ли значение как ГОТОВОЕ имя (донор)"""
    return classify(value, cjk_is_junk=False) == 'ok'


# --- семантические классы для проверки «текст от этой ли записи» (special_itemlist) ---
# Ключ — функция (колонка 5 Function_Table.txt = поле @80 записи), значение — класс.
# ★ 2026-10-13 (wave_DATA, P65): коды special_itemlist, чья запись в порту = СЕРВЕРНАЯ линия (Function_Table/Arabic),
#   а не RU/Interlude; рецепт имён их не трогает (см. раздел 4 build()).
SERVER_LINE_SPECIAL = frozenset({367, 393,
                                 # ★ 2026-10-13 вторая очередь: у кодов 1, 2, 6…17 родные RU_MINE имя/подсказка УЖЕ описывают
                                 #   серверный предмет (а запись была Interlude-линии, отсюда ложный ru_mismatch); 361 — имя
                                 #   и подсказка точного близнеца 283 (ведомость special_itemlist).
                                 1, 2, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 361,
                                 # ★ 2026-10-13 третья очередь: количество (Field88) приведено к серверу, родное RU_MINE-имя
                                 #   уже с серверным количеством («Свиток перемещения (3)»), а ru_qty ставил число Interlude.
                                 3, 18, 370, 373,
                                 # ★ 2026-10-13 четвёртая очередь (решение владельца): запись = сервер, имя/подсказка = серверные
                                 #   (Special_ItemList.txt сервера, корейские) или «-»; строки рецепта ставятся вручную (source ko/server).
                                 360, 379, 387, 389, 390, 391, 392, 399, 402, 406, 407})

FUNC_CLASS = {
    15: 'vehicle',
    16: 'exp', 19: 'exp',
    38: 'teleport', 64: 'recall', 112: 'warehouse',
    48: 'revival',
    42: 'hp', 47: 'regen', 52: 'hp', 6: 'hp', 7: 'mp',
    1: 'atk', 2: 'matk',
    210: 'repair', 211: 'repair',
    97: 'upgrade', 98: 'upgrade',
    35: 'reset', 36: 'reset',
}
# Слова-маркеры класса в тексте (имя + подсказка). RU и EN вперемешку — тексты одноязычные,
# лишние варианты просто не встретятся.
CLASS_WORDS = {
    'vehicle':   (r'скорост', r'метл', r'мустанг', r'циклон', r'птичк', r'свинк', r'шарик',
                  r'кролик', r'доск', r'байк', r'сани', r'нимб', r'облач', r'цито', r'динозавр',
                  r'бустер', r'трубк', r'ков[ёе]р', r'дракон', r'вспышк', r'передвиж',
                  r'moving speed', r'vehicle', r'cyclone', r'mustang', r'nimbus', r'bizm',
                  r'pang', r'coco', r'hatchling', r'sled', r'\bboard\b', r'booster'),
    'exp':       (r'опыт', r'уровн', r'\bexp\b', r'experience'),
    'teleport':  (r'телепорт', r'перемещ', r'teleport'),
    'recall':    (r'призыв', r'возврат', r'recall'),
    'warehouse': (r'хранилищ', r'хранен', r'склад', r'warehouse'),
    'pet':       (r'питомц', r'кормить', r'\bpet\b'),
    'revival':   (r'оживл', r'воскр', r'revival', r'revive'),
    'hp':        (r'\bоз\b', r'здоров', r'эликсир', r'\bhp\b', r'элемент', r'element'),
    'mp':        (r'\bом\b', r'\bmp\b'),
    'regen':     (r'\bлеч', r'восстан', r'reg\.', r'регенер', r'recover', r'regener'),
    'atk':       (r'атак', r'attack'),
    'matk':      (r'маги', r'magic'),
    'repair':    (r'ремонт', r'почин', r'restore', r'repair'),
    'upgrade':   (r'улучшен', r'заточ', r'upgrade'),
    'reset':     (r'сброс', r'умен', r'навык', r'reset'),
}
CLASS_RE = {cls: [re.compile(w, re.U) for w in words] for cls, words in CLASS_WORDS.items()}


def text_classes(text):
    """множество семантических классов, обнаруженных в тексте"""
    t = (text or '').lower()
    return {cls for cls, pats in CLASS_RE.items() if any(p.search(t) for p in pats)}


def contradicts(text, sub):
    """True, если текст ЯВНО описывает не ту функцию, что записана в самой записи.
    Требуется: класс функции известен, в тексте он НЕ найден, но найден другой класс."""
    want = FUNC_CLASS.get(sub)
    if not want or not usable(text):
        return False
    found = text_classes(text)
    if not found:
        return False           # текст ничего не сообщает — не обвиняем
    return want not in found


RE_PARENS = re.compile(r'\(([^)]*)\)')
RE_TIMEUNIT = re.compile(r'мин|час|дн|ден|год|сек|day|hour|min|year|sec|week|недел', re.U)


def parens_qty(name):
    """Число зарядов из ПОСЛЕДНИХ скобок имени: '(20+4)'->24. None, если скобок нет
    или в них указано ВРЕМЯ («30 мин», «3 days») — это длительность, а не заряды."""
    m = RE_PARENS.findall(name or '')
    if not m:
        return None
    last = m[-1]
    if RE_TIMEUNIT.search(last.lower()):
        return None
    nums = re.findall(r'\d+', last)
    return sum(int(x) for x in nums) if nums else None


def qty_contradicts(ru_name, en_name, qty):
    """RU-имя обещает не то число зарядов, что лежит в записи, а EN-имя — то самое."""
    if qty <= 0:
        return False
    a, b = parens_qty(ru_name), parens_qty(en_name)
    return a is not None and b is not None and b == qty and a != qty


def fix_qty_in_name(ru_name, en_name):
    """Чинит ТОЛЬКО число зарядов в русском имени, сам перевод сохраняя:
    «Зелье лечения (10)» + «Event Regeneration Potion(50)» -> «Зелье лечения (50)».
    Так исправляется сопоставление, но локализация не теряется."""
    src = RE_PARENS.findall(en_name or '')
    if not src:
        return None
    repl = src[-1]
    spans = [m for m in RE_PARENS.finditer(ru_name or '')]
    if not spans:
        return None
    m = spans[-1]
    return (ru_name[:m.start(1)] + repl + ru_name[m.end(1):]).strip()


# ---------------------------------------------------------------- сборка слоя

def pick_name(code, en, ko_ok, ar, srv, legacy):
    """источник имени по приоритету en > ko > srv(английское) > ar > srv > legacy"""
    if en is not None:
        t, k = en.get('name', ''), en.get('kind', '')
        if usable(t):
            return t, ('ko' if k == 'ko' else 'en')
    # ★ ИСПРАВЛЕНИЕ (11.10.2026): правило владельца 27.09 «приоритет RU>EN>AR» (`Reports/owner_0927/items_data_0927.md`,
    #   пасхальные коробки 53662–53665) — АНГЛИЙСКОЕ имя сервера идёт раньше арабского имени клиента. Раньше это жило только
    #   ручными строками рецепта (source=srv), и пересборка возвращала арабские; на живом доноре таких имён 129+4.
    if usable(srv or '') and _is_clean_english(srv):
        return srv, 'srv'
    if ar is not None and usable(ar.get('name', '')):
        return ar['name'], 'ar'
    if usable(srv or ''):
        return srv, 'srv'
    if usable(legacy or ''):
        return legacy, 'legacy'
    return None, None


def _is_clean_english(s):
    """Имя целиком из печатного ASCII, без «?» (след потерянной кодировки) и с буквами — английское имя сервера."""
    s = (s or '').strip()
    return (bool(s) and all(32 <= ord(c) < 127 for c in s) and '?' not in s
            and re.search(r'[A-Za-z]{2}', s) is not None)


def _is_japanese(s):
    """Есть японская кана (U+3040…U+30FF) и нет хангыля — текст японский."""
    s = s or ''
    return (any(0x3040 <= ord(c) <= 0x30FF for c in s)
            and not any(0xAC00 <= ord(c) <= 0xD7A3 for c in s))


def add(rows, table, key, column, old, new, source, reason, note=''):
    if new is None or not usable(new):
        return False
    if (new or '').strip() == (old or '').strip():
        return False
    # ★ 27.09.2026 (itemsdatafix, МЕЛК-2 ревизии items_data): донор EN-клиента местами несёт ЯПОНСКИЙ
    #   текст (cp949, катакана: 71399 «ブル?デ?モンヘルム»…) — 162 имени + 54 описания были помечены «en».
    #   Метка источника описывает ЯЗЫК значения, поэтому такой текст метится 'ja' (донор тот же).
    if source == 'en' and _is_japanese(new):
        source = 'ja'
    rows.append([table, key, column, old, new.strip(), source, reason, note])
    return True


def build():
    st = collections.Counter()

    print('чтение RU/EN/AR клиентов и серверных таблиц…')
    ru_il = load_item_list(RU_BBZ, 'cp1251')
    en_il = load_item_list(EN_BBZ, 'cp1252', try_ko=True)
    ar_il = load_item_list(AR_BBZ, 'cp1256') if os.path.isfile(AR_BBZ) else {}
    srv_nm = load_server_names()

    ru_it = load_item_tooltip(RU_BBZ, 'cp1251')
    en_it = load_item_tooltip(EN_BBZ, 'cp1252', try_ko=True)

    ru_sw = load_sowell_list(RU_BBZ, 'cp1251')
    en_sw = load_sowell_list(EN_BBZ, 'cp1252', try_ko=True)
    ru_swt = load_sowell_tooltip(RU_BBZ, 'cp1251')
    en_swt = load_sowell_tooltip(EN_BBZ, 'cp1252', try_ko=True)

    ru_sp = load_special(RU_BBZ, 'cp1251')
    en_sp = load_special(EN_BBZ, 'cp1252', try_ko=True)
    ar_sp = load_special(AR_BBZ, 'cp1256') if os.path.isfile(AR_BBZ) else {}
    ru_spt = load_special_tooltip(RU_BBZ, 'cp1251')
    en_spt = load_special_tooltip(EN_BBZ, 'cp1252', try_ko=True)

    legacy = {}
    if os.path.isfile(LEGACY):
        with io.open(LEGACY, 'r', encoding='utf-8-sig', newline='') as f:
            for r in csv.DictReader(f):
                k = (r.get('itemId') or '').strip()
                if k.lstrip('-').isdigit():
                    legacy[int(k)] = (r.get('name') or '').strip()
    print(f'  item_list RU={len(ru_il)} EN={len(en_il)} AR={len(ar_il)}; сервер имён={len(srv_nm)}; '
          f'прежний слой={len(legacy)}')

    rows = []

    # ---- 1. item_list.ItemName ----
    for code in sorted(ru_il):
        old = ru_il[code]['name']
        cl = classify(old)
        if cl == 'ok':
            continue
        new, src = pick_name(code, en_il.get(code), True, ar_il.get(code),
                             srv_nm.get(code), legacy.get(code))
        reason = 'ru_missing' if cl == 'missing' else 'ru_junk'
        if add(rows, 'item_list', code, 'ItemName', old, new, src, reason):
            st['item_list/' + reason + '/' + src] += 1
        else:
            st['item_list/' + reason + '/НЕТ ДОНОРА'] += 1

    # ---- 2. item_tooltip.Description (только там, где RU-описания нет) ----
    for code in sorted(ru_it):
        old = ru_it[code]['text']
        if classify(old) == 'ok':
            continue
        e = en_it.get(code)
        new = e['text'] if e else None
        if add(rows, 'item_tooltip', code, 'Description', old, new, 'en', 'ru_missing'):
            st['item_tooltip/ru_missing/en'] += 1
        else:
            st['item_tooltip/ru_missing/НЕТ ДОНОРА'] += 1

    # ---- 3. sowell_list.Name / sowell_tooltip.Text ----
    for code in sorted(ru_sw):
        old = ru_sw[code]['name']
        cl = classify(old)
        if cl == 'ok':
            continue
        e = en_sw.get(code)
        new, src = (e['name'], ('ko' if e['kind'] == 'ko' else 'en')) if e else (None, None)
        reason = 'ru_missing' if cl == 'missing' else 'ru_junk'
        if add(rows, 'sowell_list', code, 'Name', old, new, src, reason):
            st['sowell_list/' + reason + '/' + src] += 1
        else:
            st['sowell_list/' + reason + '/НЕТ ДОНОРА'] += 1
    for code in sorted(ru_swt):
        old = ru_swt[code]['text']
        if classify(old) == 'ok':
            continue
        e = en_swt.get(code)
        if add(rows, 'sowell_tooltip', code, 'Text', old, e['text'] if e else None,
               'en', 'ru_missing'):
            st['sowell_tooltip/ru_missing/en'] += 1
        else:
            st['sowell_tooltip/ru_missing/НЕТ ДОНОРА'] += 1

    # ---- 4. special_itemlist.Name + specialitem_tooltiplist.Name/Tooltip ----
    #  а) плейсхолдеры и мусор — как везде;
    #  б) ru_mismatch — RU-текст противоречит функции СВОЕЙ записи, а EN — соответствует.
    #  Решение принимается ПО КАЖДОМУ ПОЛЮ отдельно: у кодов 27/28/29 имя верное, а подсказка
    #  чужая; у 300/301/302/309 — наоборот. Валить оба поля скопом нельзя.
    mismatch = {}      # code -> {поле: причина}
    for code in sorted(ru_sp):
        # ★ 2026-10-13 (wave_DATA, P65): у кодов серверной линии имя/подсказку НЕ берём из EN Interlude — там они
        #   «свиток обмена опций», а на сервере (Function_Table) 367 = премиум-бафф (тип 17), 393 = транспорт (тип 15);
        #   запись порта приведена к серверу ведомостью `_value_overlay/special_itemlist.csv`, имя — «-» как у RU_MINE
        #   и Arabic. Разбор — `Reports/anticheat/wave_DATA_20261013.md` §1.
        if code in SERVER_LINE_SPECIAL:
            continue
        r = ru_sp[code]
        e = en_sp.get(code)
        rt = ru_spt.get(code, {})
        et = en_spt.get(code, {})
        sub = r['sub']
        en_name = (e or {}).get('name', '')
        en_tip = et.get('text', '')
        note = ('функция %d(%s) qty=%d dur=%d' %
                (sub, FUNC_CLASS.get(sub, '?'), r['qty'], r['dur']))

        # --- имя предмета ---
        cl = classify(r['name'])
        reason = None
        if cl == 'missing':
            reason = 'ru_missing'
        elif cl == 'junk':
            reason = 'ru_junk'
        elif usable(en_name) and contradicts(r['name'], sub) and not contradicts(en_name, sub):
            reason = 'ru_mismatch'
        elif qty_contradicts(r['name'], en_name, r['qty']):
            reason = 'ru_qty'
        if reason:
            if reason in ('ru_mismatch', 'ru_qty'):
                mismatch.setdefault(code, {})['Name'] = reason
            if reason == 'ru_qty':
                # локализацию сохраняем, правим только число зарядов
                new, src = fix_qty_in_name(r['name'], en_name), 'en_qty'
            else:
                new, src = pick_name(code, e, True, ar_sp.get(code), None, None)
            if add(rows, 'special_itemlist', code, 'Name', r['name'], new, src, reason, note):
                st['special_itemlist/' + reason + '/' + src] += 1
            else:
                st['special_itemlist/' + reason + '/НЕТ ДОНОРА'] += 1
            # имя в таблице подсказок дублирует имя предмета — двигаем синхронно
            tip_new = (fix_qty_in_name(rt.get('name', ''), et.get('name', ''))
                       if reason == 'ru_qty' else et.get('name') if et else None)
            if et and add(rows, 'specialitem_tooltiplist', code, 'Name',
                          rt.get('name', ''), tip_new, src, reason, note):
                st['specialitem_tooltiplist/' + reason + '/Name'] += 1

        # --- подсказка предмета ---
        cl_t = classify(rt.get('text', ''))
        reason_t = None
        if cl_t in ('missing', 'junk'):
            reason_t = 'ru_missing' if cl_t == 'missing' else 'ru_junk'
        elif usable(en_tip) and contradicts(rt.get('text'), sub) and not contradicts(en_tip, sub):
            reason_t = 'ru_mismatch'
        if reason_t and et:
            if reason_t == 'ru_mismatch':
                mismatch.setdefault(code, {})['Tooltip'] = reason_t
            if add(rows, 'specialitem_tooltiplist', code, 'Tooltip',
                   rt.get('text', ''), en_tip, 'en', reason_t, note):
                st['specialitem_tooltiplist/' + reason_t + '/Tooltip'] += 1
        # имя в таблице подсказок, если оно само по себе пустое
        if et and classify(rt.get('name', '')) != 'ok' and not reason:
            if add(rows, 'specialitem_tooltiplist', code, 'Name',
                   rt.get('name', ''), et.get('name'), 'en', 'ru_missing', note):
                st['specialitem_tooltiplist/ru_missing/Name'] += 1
    mismatch_codes = mismatch

    # дедупликация (table,key,column) — приоритет у первой записи (mismatch идёт раньше)
    seen = set()
    uniq = []
    for r in rows:
        k = (r[0], r[1], r[2])
        if k in seen:
            continue
        seen.add(k)
        uniq.append(r)
    uniq.sort(key=lambda r: (r[0], int(r[1]), r[2]))
    return uniq, st, mismatch_codes, dict(ru_sp=ru_sp, en_sp=en_sp, ru_spt=ru_spt, en_spt=en_spt,
                                          ru_il=ru_il, en_il=en_il, srv_nm=srv_nm, ar_il=ar_il,
                                          legacy=legacy)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--stats-only', action='store_true')
    ap.add_argument('--out', default=OUT_CSV)
    a = ap.parse_args(argv)

    rows, st, mism, ctx = build()

    # ★★ 2026-09-27 (itemsfix). ОБЪЕДИНЯТЬ, НЕ ЗАМЕНЯТЬ. В рецепт дописывают строки и вручную
    #   (27.09: 160 строк reason=ru_mismatch по списку n3_candidates.json). Пересборка их не порождает,
    #   и прежде молча стёрла бы. Теперь строки ДЕЙСТВУЮЩЕГО рецепта (OUT_CSV), которых нет в сборке,
    #   переносятся как есть; совпадающие ключи берутся из сборки. Число переносов печатается.
    carried = 0
    note_carried = 0
    owner_kept = 0   # строк рецепта с source='owner', которые ПЕРЕКРЫЛИ собранную строку того же ключа
    if os.path.isfile(OUT_CSV):
        # ★ 2026-10-09 (fishbooks_69682). Строки рецепта с source='owner' — решения владельца (перевод вместо EN/AR и т.п.).
        #   Сборка порождает тот же ключ с донорским текстом и прежде молча вернула бы его. Теперь строка владельца
        #   ВЫТЕСНЯЕТ собранную строку того же ключа (table,key,column); число таких строк печатается.
        with io.open(OUT_CSV, 'r', encoding='utf-8-sig', newline='') as f:
            owner_rows = {(r['table'], r['key'], r['column']): [r[c] for c in OVERLAY_HEADER]
                          for r in csv.DictReader(f) if (r.get('source') or '').strip() == 'owner'}
        if owner_rows:
            kept_rows = []
            for r in rows:
                k = (r[0], str(r[1]), r[2])
                if k in owner_rows:
                    owner_kept += 1
                    continue
                kept_rows.append(r)
            rows[:] = kept_rows + list(owner_rows.values())
        built = {(r[0], str(r[1]), r[2]) for r in rows}
        # ★ 2026-09-27 (MELK-3): note ДЕЙСТВУЮЩЕГО рецепта не терять. У сборки note почти всегда пусто,
        #   поэтому для совпадающего ключа, если у собранной строки note пустой, а в рецепте — есть,
        #   переносим note (пометки 27.09: Q16, ru_mismatch и т.п.). Значения при этом не меняются.
        NOTE_IDX = OVERLAY_HEADER.index('note')
        cur_notes = {}
        with io.open(OUT_CSV, 'r', encoding='utf-8-sig', newline='') as f:
            for r in csv.DictReader(f):
                k = (r['table'], r['key'], r['column'])
                if r.get('note', '').strip():
                    cur_notes[k] = r['note']
                if k in built:
                    continue
                rows.append([r[c] for c in OVERLAY_HEADER])
                built.add(k)
                carried += 1
        for row in rows:
            k = (row[0], str(row[1]), row[2])
            if not str(row[NOTE_IDX]).strip() and k in cur_notes:
                row[NOTE_IDX] = cur_notes[k]
                note_carried += 1
        rows.sort(key=lambda r: (r[0], int(r[1]), r[2]))
    st['перенесено из действующего рецепта (ручные строки)'] = carried
    st['перенесено note из действующего рецепта'] = note_carried
    st['строк владельца (source=owner) вытеснили собранные'] = owner_kept

    print('\n=== состав слоя ===')
    for k in sorted(st):
        print('  %-58s %6d' % (k, st[k]))
    print('  %-58s %6d' % ('ВСЕГО СТРОК СЛОЯ', len(rows)))
    print('\nтекст не от этой записи (ru_mismatch/ru_qty), кодов: %d' % len(mism))
    for c in sorted(mism):
        print('    %4d  %s' % (c, mism[c]))

    if a.stats_only:
        return 0
    # Каталог рецепта лежит вне `Content/` и в чистом клоне может отсутствовать.
    out_dir = os.path.dirname(os.path.abspath(a.out))
    if out_dir and not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    with io.open(a.out, 'w', encoding='utf-8-sig', newline='') as f:
        w = csv.writer(f, lineterminator='\r\n')
        w.writerow(OVERLAY_HEADER)
        w.writerows(rows)
    print('\nзаписано: %s (%d строк)' % (a.out, len(rows)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
