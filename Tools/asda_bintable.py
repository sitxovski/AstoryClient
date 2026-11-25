# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_bintable.py — чтение `.bin`-таблиц клиента AsdaStory и ЕДИНЫЙ строковый кодек (CP1251 + шифр игры).

Волна 2 аудита дублей (`Reports/dedup_audit/00_PLAN.md` E15/п.8, `03_tools_python.md` T07/T14/T15,
`04_cross_layer.md` D8; отчёт `wave2_formats.md`). Здесь одна реализация того, что было скопировано:
  · заголовок 20 б и обход записей (5+ копий `read_rows/parse_bin/extract`);
  · C-строка фиксированного поля с ЯВНОЙ кодировкой (13 копий `cstr/_cstr` в 4 кодировках);
  · выбор кодировки по содержимому `pick_enc` (3 копии в 2 вариантах);
  · шифр строк «байт − 1» и его терминаторы (3 реализации: `decode_item_db.dec_str`, дословная копия
    в `asda_build_name_overlay.py`, параметризованная в `asda_title_sources.py`);
  · кодек CP1251 порта — Python-пара к C++ `FAsdaLoginData::DecodeCp1251/EncodeCp1251`
    (`Source/AsdaReborn/Private/Login/AsdaLoginData.cpp`).

ЗАГОЛОВОК ТАБЛИЦЫ (20 б): f32 ver@0, i32 count@4, i32 extra@8, char[4] tag@12, i32 pad@16.
  Раскладка A: записи с 0x14. Раскладка B (`extract_text_tables`): доп. u32 count@0x14, записи с 0x18.
  Семейство с MD5: у каждой записи 16-байтный префикс, поля шифрованы.

★★★ ШИФР СТРОК (закон `item-string-cipher-terminator`). Байт открытого текста = байт файла − 1.
  В шифре буква «я» (CP1251 0xFF) — это 0x00, поэтому 0x00 НЕ конец строки. Терминатор В ШИФРОВАННОМ
  виде зависит от поколения файла:
      TERM_AUTO (None) — `decode_item_db`: есть 0x01 → конец там (хвост поля — заполнитель 0x01);
                         0x01 нет → старый формат, конец по первому 0x00. Для Bin.bbz нашего клиента.
      TERM_RU    0x01  — наш RawAssets / out_client, строго: нет 0x01 → строка занимает всё поле.
      TERM_FANTA 0x7F  — Donors/TRANSLATE_unpacked/tables_fanta (проверено побайтово в `asda_title_sources`).
      TERM_NUL   0x00  — английский оригинал (в английском «я» не бывает).
  ★ D8: режим AUTO на fanta-файле режет строку на первой «я» (0x7F там нет, 0x01 тоже) — именно так
    появляются «битые описания». Терминатор задаётся ЯВНО по источнику; AUTO — только для таблиц,
    где заведомо 0x01 или старый формат.

КОДИРОВКИ. У одного вида строк кодировка задаётся ПОЛЕМ реестра таблицы, не угадыванием:
  cp1251 — русский текст клиента; cp949 — корейский/английский с корейской пунктуацией;
  latin1 — имена файлов/узлов (ASCII, декод без ошибок); cp1256 — арабская сборка.
  Угадывание — только `pick_enc` (langdet), и только там, где в одной таблице смешаны языки.

★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): только определения;
  `asda_paths`/langdet подгружаются лениво внутри `pick_enc`.
"""
import struct
from collections import namedtuple

# ─────────────────────────────────────────────────────────────── заголовок и записи
HDR = 20                    # общий заголовок семейства .bin
HDR_B = 0x18                # начало записей в раскладке B (после доп. u32 count@0x14)
MD5_PREFIX = 16             # длина MD5-префикса записи у шифрованного семейства

TableHeader = namedtuple("TableHeader", "ver count extra tag pad")


def read_header(data):
    """Заголовок 20 б: (ver f32, count i32, extra i32, tag bytes[4], pad i32). Буфер короче — ValueError."""
    if len(data) < HDR:
        raise ValueError("файл короче заголовка таблицы (%d < %d)" % (len(data), HDR))
    ver, count, extra = struct.unpack_from("<fii", data, 0)
    return TableHeader(ver, count, extra, bytes(data[12:16]), struct.unpack_from("<i", data, 16)[0])


def tag_is(data, tag):
    """Сравнить магию таблицы на +12 с `tag` (обычно 3 буквы, например b"EFF")."""
    return bytes(data[12:12 + len(tag)]) == tag


def record_count(data, layout_b=False, signed=True):
    """Число записей из заголовка: i32@4 (раскладка A) или u32@0x14 (раскладка B)."""
    if layout_b:
        return struct.unpack_from("<I", data, HDR)[0]
    return struct.unpack_from("<i" if signed else "<I", data, 4)[0]


def iter_records(data, stride, *, count=None, layout_b=False, base=None, md5=0):
    """Обойти записи: yield (i, запись) — запись уже без MD5-префикса (`md5` байт).

    count  — None: взять из заголовка (`record_count`); отрицательный count даёт пустой обход.
    base   — None: 0x14 (A) / 0x18 (B). Запись, не влезающая в файл целиком, обход ОБРЫВАЕТ
             (так делали все копии: `if o + stride > len(d): break`).
    """
    if count is None:
        count = record_count(data, layout_b)
    if base is None:
        base = HDR_B if layout_b else HDR
    n = len(data)
    for i in range(count):
        o = base + i * stride
        if o + stride > n:
            break
        yield i, data[o + md5:o + stride]


# ─────────────────────────────────────────────────────────────── C-строки
def cstr_bytes(b, off=0, maxlen=None):
    """Байты поля до первого NUL. maxlen=None — до конца буфера (поле без фиксированной длины)."""
    raw = b[off:] if maxlen is None else b[off:off + maxlen]
    z = raw.find(b"\0")
    return raw if z < 0 else raw[:z]


def cstr(b, off, maxlen, enc, errors="strict"):
    """C-строка фиксированного поля в ЯВНОЙ кодировке (без угадывания). latin1 не падает никогда."""
    return bytes(cstr_bytes(b, off, maxlen)).decode(enc, errors)


def decode_text(raw, encoding, fallback="latin-1"):
    """Декод с запасным путём: не декодировалось в `encoding` — `fallback` с заменой (данные не теряем)."""
    try:
        return bytes(raw).decode(encoding)
    except Exception:
        return bytes(raw).decode(fallback, "replace")


_LANG = []                  # ленивый кэш определителя языка: [] — не загружали, [None] — нет модуля


def _langdet():
    """Общий определитель языка `AstoryClient/Tools/Translate/tools/langdet.py` (или None, если его нет)."""
    if not _LANG:
        try:
            import os
            import sys
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            import asda_paths as _P
            if _P.TRANSLATE_TOOLS not in sys.path:
                sys.path.insert(0, _P.TRANSLATE_TOOLS)
            from langdet import lang
            _LANG.append(lang)
        except Exception:
            _LANG.append(None)
    return _LANG[0]


def pick_enc(raw, on_missing="heuristic"):
    """Кодировка строки по содержимому: langdet сказал «kor» → cp949, иначе cp1251.

    on_missing — что делать без langdet: "heuristic" (декодируется как cp1251 → cp1251, иначе cp949;
    вариант `asda_import_npc_tables`/`asda_import_await_maps`) или "cp1251" (всегда cp1251; вариант
    `extract_text_tables`). ★ cp1251 падает только на 0x98, поэтому эвристика почти всегда даёт cp1251.
    """
    lang = _langdet()
    if lang is not None:
        return "cp949" if lang(raw) == "kor" else "cp1251"
    if on_missing == "cp1251":
        return "cp1251"
    try:
        bytes(raw).decode("cp1251")
        return "cp1251"
    except UnicodeDecodeError:
        return "cp949"


# ─────────────────────────────────────────────────────────────── шифр строк игры
TERM_AUTO = None            # 0x01, а если его нет — первый 0x00 (decode_item_db)
TERM_RU = 0x01              # наш клиент / RawAssets
TERM_FANTA = 0x7F           # Donors/TRANSLATE_unpacked/tables_fanta
TERM_NUL = 0x00             # английский оригинал

CIPHER_SHIFT = 1            # строки таблиц: байт − 1
ERRLIST_SHIFT = 100         # system/error list.bin: байт − 100 (StringTable_Load@0x111EC50)

_SHIFT_TABLES = {}


def _shift_table(k):
    t = _SHIFT_TABLES.get(k)
    if t is None:
        t = _SHIFT_TABLES[k] = bytes((i - k) & 0xFF for i in range(256))
    return t


def unshift(raw, k=CIPHER_SHIFT):
    """Снять сдвиг: каждый байт − k (по модулю 256)."""
    return bytes(raw).translate(_shift_table(k))


def shift(raw, k=CIPHER_SHIFT):
    """Наложить сдвиг: каждый байт + k (по модулю 256) — обратное к `unshift`."""
    return bytes(raw).translate(_shift_table((-k) & 0xFF))


def cipher_end(raw, terminator=TERM_AUTO):
    """Конец шифрованной строки в поле `raw`: (end, found).

    AUTO: первый 0x01 → (k, True); иначе первый 0x00 → (k, True); иначе (len, False).
    Явный терминатор: первый такой байт → (k, True); иначе (len, False) — текст занял всё поле.
    """
    raw = bytes(raw)
    if terminator is TERM_AUTO:
        k = raw.find(b"\x01")
        if k < 0:
            k = raw.find(b"\x00")
    else:
        k = raw.find(bytes([terminator]))
    return (k, True) if k >= 0 else (len(raw), False)


def decipher_field(raw, terminator=TERM_AUTO, ciphered=True):
    """Шифрованное строковое поле → (открытые байты строки без терминатора, full).
    full=True — терминатор не найден (строка заняла поле целиком и, значит, могла обрезаться).
    ciphered=False — поле не шифровано (старый title_list.BIN): только поиск терминатора."""
    end, found = cipher_end(raw, terminator)
    body = bytes(raw[:end])
    return (unshift(body) if ciphered else body), (not found)


def decipher_str_inplace(b, off, maxlen, terminator=TERM_AUTO):
    """Снять шифр строки ПРЯМО В bytearray записи (как `decode_item_db.dec_str`, 1:1).

    Поле = b[off : off + min(maxlen, len(b) - off)].
      AUTO: есть 0x01 → строка до него, хвост поля (заполнитель 0x01) ОБНУЛЯЕТСЯ целиком;
            0x01 нет → конец по первому 0x00 (он и остаётся нулём); нет и его — дешифруется всё поле.
      Явный терминатор: найден → строка до него, хвост обнуляется; не найден — дешифруется всё поле.
    ★ Обнуление хвоста обязательно: иначе в `_data_hex` поедут сырые 0x01 и диффы таблиц раздуются.
    """
    n = min(maxlen, len(b) - off)
    if n <= 0:
        return
    seg = bytes(b[off:off + n])
    if terminator is TERM_AUTO:
        end = seg.find(b"\x01")
        padded = end >= 0
        if not padded:
            end = seg.find(b"\x00")
            if end < 0:
                end = n
    else:
        end = seg.find(bytes([terminator]))
        padded = end >= 0
        if not padded:
            end = n
    b[off:off + end] = seg[:end].translate(_shift_table(CIPHER_SHIFT))
    if padded:
        b[off + end:off + n] = bytes(n - end)
    elif end < n:
        b[off + end] = 0                            # терминатор в дешифрованном виде


def encipher_field(plain, size, terminator=TERM_RU, pad=None, allow_full=False):
    """Открытые байты строки → шифрованное поле `size` байт (обратное к `decipher_field`).
    Строка + терминатор должны влезть: len(plain) < size. pad — байт заполнителя хвоста
    (по умолчанию = терминатор, как в формате 0x01 нашего клиента). ★ Байт открытого текста,
    который после шифра совпадёт с терминатором (0x00 при TERM_RU), делает строку неоднозначной — ValueError.
    allow_full — ★ дедуп №2 В2 (хвост Т5 волны В1): строка ровно в `size` байт пишется БЕЗ терминатора, полем целиком
      (так `decipher_field` и читает «терминатор не найден → строка заняла всё поле», full=True; пример — title_list
      запись 247, поле Desc +100, 64 б). По умолчанию выключено: прежний отказ «не входит с терминатором»."""
    plain = bytes(plain)
    term = TERM_RU if terminator is TERM_AUTO else terminator
    full = allow_full and len(plain) == size
    if len(plain) >= size and not full:
        raise ValueError("строка %d б не входит в поле %d б с терминатором" % (len(plain), size))
    body = shift(plain)
    if bytes([term]) in body:
        raise ValueError("байт строки совпадает с терминатором 0x%02X после шифра" % term)
    if full:
        return body
    fill = term if pad is None else pad
    return body + bytes([term]) + bytes([fill]) * (size - len(plain) - 1)


# ─────────────────────────────────────────────────────────────── CP1251 порта (пара к C++)
# Таблица 1:1 с `FAsdaLoginData::DecodeCp1251/EncodeCp1251` (AsdaLoginData.cpp:11-64): ASCII как есть,
# 0xC0..0xFF → А..я, плюс Ё ё « » – — … №; всё прочее → '?'. Нужна там, где инструмент обязан дать
# РОВНО то, что отправит/покажет рантайм (имена персонажей 20 б, чат) — это НЕ полный cp1251 Python.
_GAME_DEC_EXTRA = {0xA8: "Ё", 0xB8: "ё", 0xAB: "«", 0xBB: "»",
                   0x96: "–", 0x97: "—", 0x85: "…", 0xB9: "№"}
_GAME_ENC_EXTRA = {v: k for k, v in _GAME_DEC_EXTRA.items()}


def decode_game_cp1251(data, maxlen=None):
    """Как C++ DecodeCp1251: до NUL (не дальше maxlen), неизвестные байты 0x80..0xBF → '?'."""
    raw = cstr_bytes(data, 0, maxlen)
    out = []
    for c in bytes(raw):
        if c < 0x80:
            out.append(chr(c))
        elif c >= 0xC0:
            out.append(chr(0x0410 + (c - 0xC0)))
        else:
            out.append(_GAME_DEC_EXTRA.get(c, "?"))
    return "".join(out)


def encode_game_cp1251(text, max_bytes=None):
    """Как C++ EncodeCp1251: не больше max_bytes байт, БЕЗ терминатора; неизвестные символы → '?'."""
    out = bytearray()
    for ch in text:
        if max_bytes is not None and len(out) >= max_bytes:
            break
        o = ord(ch)
        if o < 128:
            out.append(o)
        elif 0x0410 <= o <= 0x044F:
            out.append(o - 0x0410 + 0xC0)
        else:
            out.append(_GAME_ENC_EXTRA.get(ch, 0x3F))
    return bytes(out)
