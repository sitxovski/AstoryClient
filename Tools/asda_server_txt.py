# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_server_txt.py — ЧТЕНИЕ СЕРВЕРНЫХ ТАБЛИЦ `.txt` ТАК, КАК ИХ ЧИТАЕТ САМ GameServer, И ОДИН ПУТЬ К ЖИВОМУ СЕРВЕРУ.

★ Дедуп №2, волна В2, пункт Т7 (`Reports/dedup_audit2/04_tools_python.md` §Т7). До него:
  • путь к живому серверу был прошит в 6 инструментах в 4 написаниях (`\\@@ASTORY_SERVER_HOSTNAME@@\h(@@ASTORY_SERVER_HOSTNAME@@_ns)`, `\\@@ASTORY_SERVER_HOSTNAME@@\H(@@ASTORY_SERVER_HOSTNAME@@_ns)`,
    `\\?\UNC\@@ASTORY_SERVER_HOSTNAME@@\…`, `//@@ASTORY_SERVER_HOSTNAME@@/…`);
  • числа серверных таблиц разбирались пятью способами: свой `_atol` на регулярке, эмуляция `sscanf`, `int()` (падает на
    «13.5», «(1)», «12abc», а сервер берёт префикс), СРАВНЕНИЕ СТРОК (`fields[3] == "4"` — для «04» сервер видит 4,
    скрипт — нет), разбор ПО СТРОКАМ и ТАБАМ (сервер читает ПОТОК слов, строки ему не важны).

КАК ЧИТАЕТ СЕРВЕР (декомпил живого образа `RE/ida/server_work/GameServer.c`, все загрузчики `ItemTable_Info\*.txt` и
`ConfigSet\*.txt` устроены одинаково; примеры — `Item_Table.txt` :68170-68312, `ExpTable.txt` :69114-69138,
`EventItem_Table.txt` :69481, `HairTypeTable.txt` :72264-72315, `PrimiumGaChaTable.txt` :75358-75399,
`PrimiumGaChaPriceTable.txt` :75436-75470):
  1. `fopen(..., "r")` — текстовый режим CRT;
  2. `fscanf(Stream, "%d", &count)` — число записей (первое слово файла);
  3. каждое поле — `fscanf(Stream, "%s", buf)` (СЛОВО до пробельного символа: пробел, \t, \n, \v, \f, \r — `isspace` в
     локали "C"), затем `j__atol(buf)` = `atol` = `strtol(buf, NULL, 10)` (`GameServer.c:141702`): ведущий знак, ДЕСЯТИЧНЫЕ
     цифры до первого не-цифрового знака, всё остальное отбрасывается («13.5» → 13, «(1)» → 0, «12abc» → 12), выход за
     int32 — насыщение до 2147483647 / −2147483648 (поведение `strtol`); 64-битные поля — `_atoi64` = `_strtoi64`;
  4. значение кладётся в поле записи нужной ширины: `*(_BYTE*)` / `*(_WORD*)` / `*(_DWORD*)` — младшие 8/16/32 бит;
  5. читается РОВНО `count` записей; лишние слова в конце файла сервер не видит, нехватка слов — мусор/нули.
  Поэтому инструмент, который сверяет или переносит серверные числа, обязан получать ТЕ ЖЕ значения: дробное «13.5»
  сервер видит как 13 (закон table-compare-atol-truncation: округление дало 19 ложных расхождений).
  ★ Ничего не теряется молча: `TokenStream` запоминает каждое слово, которое `atol` усёк не целиком (`lossy`), — инструмент
    обязан показать их в отчёте (это и есть «без усечения дробных»: значение — как у сервера, но факт усечения виден).

Модуль только читает (живой сервер — ТОЛЬКО ЧТЕНИЕ, закон server-live-path); побочных эффектов при импорте нет
(закон tools-no-import-side-effects): на уровне модуля — константы.
"""
import os

# ★ ОДИН путь к живому серверу v2 (dev-окружение владельца). Переопределение — переменная окружения ASDA_SERVER_V2
#   (её же раньше читал `asda_map_registry.SERVER_V2`; значение по умолчанию взято оттуда без изменений).
#   Проверено 11.10.2026: все четыре прежних написания открываются из Python одинаково (2396 б ExpTable.txt) —
#   замечание «обычный UNC со скобками не открывается» в шапках asda_charexp_sync/asda_hairtable_sync устарело.
LIVE_V2 = os.environ.get("ASDA_SERVER_V2", "//@@ASTORY_SERVER_HOSTNAME@@/h(@@ASTORY_SERVER_HOSTNAME@@_ns)/ASDA/Working_ASDA/v2")

# Каталоги живого GameServer (имена папок — как в `fopen` загрузчиков сервера).
LIVE_GAMESERVER = LIVE_V2 + "/GameServer"
LIVE_ITEMTABLE_INFO = LIVE_GAMESERVER + "/ItemTable_Info"
LIVE_CONFIGSET = LIVE_GAMESERVER + "/ConfigSet"

INT32_MAX = 2147483647
INT32_MIN = -2147483648
INT64_MAX = (1 << 63) - 1
INT64_MIN = -(1 << 63)

# `isspace` CRT в локали "C" — ровно эти шесть байтов (bytes.split() без аргумента режет по тем же).
_C_SPACE = b" \t\n\v\f\r"


def live_path(*parts):
    """Путь к файлу на живом сервере: `live_path("GameServer", "ItemTable_Info", "Item_Table.txt")`."""
    return "/".join([LIVE_V2.rstrip("/\\")] + [p.strip("/\\") for p in parts])


def live_itemtable(name):
    """`<v2>/GameServer/ItemTable_Info/<name>`."""
    return live_path("GameServer", "ItemTable_Info", name)


def live_configset(name):
    """`<v2>/GameServer/ConfigSet/<name>`."""
    return live_path("GameServer", "ConfigSet", name)


def live_or_snapshot(live, snapshot):
    """Живой файл, если доступен; иначе локальный снимок. Возвращает (путь, "live"|"snapshot"). Ничего не копирует."""
    if live and os.path.isfile(live):
        return live, "live"
    return snapshot, "snapshot"


def _as_bytes(tok):
    """Слово → bytes (строку кодируем latin-1: байт в байт, как его видит CRT)."""
    if isinstance(tok, (bytes, bytearray)):
        return bytes(tok)
    return str(tok).encode("latin-1", "replace")


def crt_strtol(tok, bits=32):
    """`strtol(tok, &end, 10)` CRT (bits=32) или `_strtoi64` (bits=64). Возвращает (значение, число_прочитанных_байт).

    Пробельные в начале пропускаются, затем необязательный знак и десятичные цифры ASCII; нет цифр → (0, 0).
    Выход за диапазон — насыщение (как CRT: LONG_MAX/LONG_MIN, _I64_MAX/_I64_MIN).
    """
    b = _as_bytes(tok)
    i, n = 0, len(b)
    while i < n and b[i:i + 1] in (b" ", b"\t", b"\n", b"\v", b"\f", b"\r"):
        i += 1
    neg = False
    if i < n and b[i:i + 1] in (b"+", b"-"):
        neg = b[i:i + 1] == b"-"
        i += 1
    j = i
    while j < n and 0x30 <= b[j] <= 0x39:
        j += 1
    if j == i:
        return 0, 0
    v = int(b[i:j])
    v = -v if neg else v
    lo, hi = (INT32_MIN, INT32_MAX) if bits == 32 else (INT64_MIN, INT64_MAX)
    return max(lo, min(hi, v)), j


def crt_atol(tok):
    """`atol` сервера (`j__atol` → `strtol(…, 10)`): префикс-число, насыщение int32. «13.5» → 13, «(1)» → 0."""
    return crt_strtol(tok, 32)[0]


def crt_atoi64(tok):
    """`_atoi64` сервера (`_strtoi64(…, 10)`): то же для 64-битных полей (накопленный опыт ExpTable)."""
    return crt_strtol(tok, 64)[0]


def crt_scanf_d(tok):
    """`fscanf("%d")` на слове: (значение, True) при успехе, (None, False) — нет цифр (переменная не тронута)."""
    v, used = crt_strtol(tok, 32)
    return (v, True) if used else (None, False)


def as_u8(v):
    """Значение, положенное в `*(_BYTE*)` (младшие 8 бит, без знака)."""
    return v & 0xFF


def as_i8(v):
    """Значение `*(_BYTE*)`, прочитанное как `char` со знаком."""
    v &= 0xFF
    return v - 0x100 if v & 0x80 else v


def as_u16(v):
    """Значение, положенное в `*(_WORD*)` (младшие 16 бит, без знака)."""
    return v & 0xFFFF


def as_i16(v):
    """Значение `*(_WORD*)`, прочитанное как `__int16` (так сервер сравнивает индексы строк)."""
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def as_i32(v):
    """Значение `*(_DWORD*)` как int32."""
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def is_full_int(tok):
    """True — `atol` прочитал слово ЦЕЛИКОМ (нет дробной части/мусора, которые сервер отбросил)."""
    b = _as_bytes(tok)
    return len(b) > 0 and crt_strtol(b, 64)[1] == len(b)


def read_tokens(path):
    """Все слова файла так, как их выдаёт `fscanf("%s")` (bytes; разделители — `isspace` локали "C")."""
    with open(path, "rb") as f:
        return f.read().split()


class TokenStream:
    """Поток слов серверного `.txt` с чтением полей так, как это делает загрузчик сервера.

    ts = TokenStream(path)
    n = ts.count()          # fscanf("%d") — число записей
    for i in range(n):
        idx = ts.i32()      # fscanf("%s") + atol
        name = ts.word()    # fscanf("%s") — строковое поле (bytes)
    ts.rest()               # слова ПОСЛЕ прочитанного — сервер их не видит
    ts.lossy                # [(номер_слова, слово)] — где atol отбросил хвост (13.5 → 13, (1) → 0)
    """

    def __init__(self, path=None, tokens=None):
        self.path = path
        self.toks = list(tokens) if tokens is not None else read_tokens(path)
        self.pos = 0
        self.lossy = []
        self.short = False      # True — сервер попытался прочитать слово за концом файла

    def word(self):
        """Следующее слово (bytes) или b"" за концом файла (fscanf не прочитал — буфер обнулён `memset`)."""
        if self.pos >= len(self.toks):
            self.short = True
            self.pos += 1
            return b""
        t = self.toks[self.pos]
        self.pos += 1
        return t

    def _num(self, bits):
        at = self.pos
        t = self.word()
        v, used = crt_strtol(t, bits)
        if t and used != len(t):
            self.lossy.append((at, t))
        return v

    def count(self):
        """Число записей: `fscanf("%d")` (нет цифр → 0, как обнулённая переменная загрузчика)."""
        v = self._num(32)
        return v

    def i32(self):
        """Поле `*(_DWORD*) = atol(слово)`."""
        return self._num(32)

    def i64(self):
        """Поле `*(_QWORD*) = _atoi64(слово)`."""
        return self._num(64)

    def u8(self):
        """Поле `*(_BYTE*) = atol(слово)` (младшие 8 бит)."""
        return as_u8(self._num(32))

    def i16(self):
        """Поле `*(_WORD*) = atol(слово)`, прочитанное как `__int16`."""
        return as_i16(self._num(32))

    def u16(self):
        """Поле `*(_WORD*) = atol(слово)` без знака."""
        return as_u16(self._num(32))

    def rest(self):
        """Слова после прочитанного (сервер их не читает): список bytes."""
        return self.toks[self.pos:] if self.pos < len(self.toks) else []

    def lossy_text(self, limit=10, enc="cp1251"):
        """Строка-отчёт об усечённых словах: «слово №N '13.5'→13, …» (пусто — усечений не было)."""
        out = []
        for at, t in self.lossy[:limit]:
            out.append("слово №%d %r→%d" % (at, t.decode(enc, "replace"), crt_atol(t)))
        if len(self.lossy) > limit:
            out.append("… ещё %d" % (len(self.lossy) - limit))
        return ", ".join(out)


def read_counted_records(path, nfields, tokens=None):
    """Таблица «число записей + записи по `nfields` слов» (все поля читаются словами).

    Возвращает (declared, records, ts): records — список списков bytes длиной nfields, ts — поток (lossy/rest/short).
    Сервер читает ровно `declared` записей; хвост файла (`ts.rest()`) он не видит.
    """
    ts = TokenStream(path, tokens)
    declared = ts.count()
    recs = []
    for _ in range(max(0, declared)):
        recs.append([ts.word() for _ in range(nfields)])
    return declared, recs, ts
