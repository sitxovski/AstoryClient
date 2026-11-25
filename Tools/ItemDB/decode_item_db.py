#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
decode_item_db.py — ОБОБЩЁННЫЙ 1:1-декодер ВСЕХ таблиц AsdaStory Bin.bbz -> CSV + JSON.

Слой данных для item-БД UE5-порта. Реверс форматов подтверждён в отчётах:
  Reports/item_table_format.md   (item_list 248 / item_img 300)
  Reports/item_db_formats_A.md   (15 таблиц предметов)
  Reports/item_db_formats_B.md   (13 экономика/персонаж)
  Reports/item_db_formats_C.md   (19 камни/титулы/питомцы/скиллы)

Единый формат (эталон CPackFile/.bbz = стандартный ZIP):
  Таблица = header 20б (float ver@0 / int count@4 / int extra@8 / char[4] tag@12 / int pad@16)
            + count × diskStride.
  Два семейства:
    (A) MD5-префикс 16б на запись + шифр полей (DWORD/WORD -const, строки -1/байт, CP1251)
    (B) raw без шифра.
  Нестандартные (.ssi / .sif) — без header (raw stride, count = size/stride).
  refineupmoney — фикс. блок 40б (count в header игнорируется загрузчиком).

Валидация: count(header) == (usize - headerSize) / diskStride побайтово; иначе помечается.
Данные не теряются: таблицы с неполной пофилдовой раскладкой доп. пишут _data_hex
(полная запись после снятия MD5 и штатной дешифровки — 1:1 восстановима).

Запуск:
  python decode_item_db.py [Bin.bbz] [--out DIR] [--only a.bin,b.bin] [--no-ledgers] [--accept-diff] [--no-dedup-map]

★★★ ДЕДУПЛИКАЦИЯ ПРЕДМЕТОВ (2026-11-01, item_dedup этап 2, решение владельца). После ведомостей декодер перекодирует
  ССЫЛКИ на предметы-дубли (бустеры, коробки, премиум-гача, обмены +68/+108, стартовая экипировка, разборка, крафт,
  высокая ковка) на основной код по карте AstoryClient/Tools/data/item_dedup_map.json (общий модуль
  AstoryClient/Tools/asda_item_dedup.py, функция apply_dedup_map). Строки самих дублей не трогаются. В сводке —
  «item_dedup: перекодировано N». `--no-dedup-map` — без шага (аудит); в боевой каталог без шага — ОТКАЗ.

★★★ ЗАЩИТА БОЕВОГО КАТАЛОГА (2026-09-27, itemsdatafix, КРИТ-1 ревизии items_data). 27.09 полный декод
  дважды выгрузили в боевой RawAssets/Data/ItemDB — гача (583→356 строк), русские звания и серверные
  пороги опыта откатились к архиву (жили только в CSV; теперь — в ведомостях). С тех пор:
  • без --only в боевой каталог — ОТКАЗ (код 2): полный декод только в --out DIR;
  • --no-ledgers в боевой каталог — ОТКАЗ;
  • с --only декод идёт в Tools/_out/decode_stage/<время>, там же слой имён; в боевой каталог таблица
    копируется, только если её CSV совпал с боевым по всем колонкам, либо с --accept-diff (с бэкапом
    боевых CSV/JSON в _backup/<дата>_decode_itemdb/). Отказ по любой таблице — код 3.
  Пост-шаги, пишущие CSV напрямую (гача/звания/опыт), после запуска переносить в ведомости:
    python Tools/build_itemdb_poststep_ledgers.py --table <т> --write --replace

★★★ ВЕДОМОСТИ ОБЪЕДИНЕНИЯ (2026-09-25). Поверх архива на каждом декоде накладываются ведомости
  AstoryClient/RawAssets/ItemDB/_tail_append/<таблица>.csv   — записи, дописываемые в ХВОСТ;
  AstoryClient/RawAssets/ItemDB/_value_overlay/<таблица>.csv — замены значений «было → стало».
  Поэтому перегенерация больше НЕ стирает обогащение event_item_list / item_upgrade_cost /
  item_tooltip и серверные цены hairtable. Ведомости строят Tools/build_itemdb_ledgers.py и
  AstoryClient/Tools/asda_hairtable_sync.py --apply. `--no-ledgers` — чистый декод 1:1 с архивом
  (для аудита; в боевой каталог порта так НЕ выгружать). Не легла ведомость — статус таблицы CHECK.
  ★ С 27.09 слой-рецепт имён (шаг 3 шапки asda_apply_name_overlay.py) при выгрузке в боевой каталог
    накладывается автоматически в конце прогона — см. блок «ИМЕНА ПРЕДМЕТОВ ПОСЛЕ РЕГЕНЕРАЦИИ».

★★★ ИМЕНА ПРЕДМЕТОВ ПОСЛЕ РЕГЕНЕРАЦИИ (исправлено 2026-09-27, itemsfix; прежний текст 01.08 УСТАРЕЛ).
Скрипт ПЕРЕЗАПИСЫВАЕТ item_list/item_tooltip/sowell_*/special* «как в клиенте», а в RU-клиенте
43 221 предмет из 58 878 имеет имя «-». Имена/описания возвращает ТОЛЬКО слой-рецепт:
  python AstoryClient\Tools\asda_apply_name_overlay.py       # наложить рецепт (все 6 таблиц)
  ASDA_TABLES=item_list,…  run_headless_safe.py AstoryClient\Tools\_import_one_itemdb_table.py
⚠ `asda_fill_item_names.py` (слой item_names_recovered.csv, 01.08) ЗАПРЕЩЁН и сразу выходит:
  25.09.2026 финальная синхронизация прогнала его по этой шапке и откатила 1 559 имён
  (восстановлены 27.09: 1 398 + 161). Импортёр `_import_one_itemdb_table.py` теперь сам отказывает
  в импорте item-таблиц, если рецепт применён не полностью (гейт, см. его шапку).
⚠ Проверка «до боевого каталога»: выгружать в отдельную папку (--out DIR), затем
  `asda_apply_name_overlay.py --itemdb DIR` — и сравнивать с боевыми CSV по ключу.

★★★ ВНИМАНИЕ (2026-09-16). НАД item_list.csv ЛЕЖИТ СЕРВЕРНЫЙ ОВЕРЛЕЙ — РЕГЕНЕРАЦИЯ ЕГО СНОСИТ.
  13.08.2026 `Tools/port_itemdb_union_from_server.py` проставил 316 записям порта серверные
  ItemType/ItemSubType/DurationOrEnchant (все они — коробки: тип 11, подтип 200/201). Ведомость —
  `AstoryClient/RawAssets/ItemDB/_server_union_overlay.csv` (316 кодов × 3 поля), обоснование —
  `Reports/server_package_tables_sync.md` §4.1/§10 п.3 и `Reports/dt_reimport_union.md`.
  Без оверлея на живом сервере эти 316 коробок не открываются (сервер считает их коробками, а
  клиент-эталон Chronicles Interlude — оружием/бронёй).
  ПОЭТОМУ ШАГОВ ПОСЛЕ РЕГЕНЕРАЦИИ item_list.bin — ТРИ, и оверлей идёт ПЕРВЫМ:
    (исторический порядок 16.09; ★ с 25.09 коробки накладывает сам декодер ведомостью значений,
     а имена — `asda_apply_name_overlay.py`; `asda_fill_item_names.py` ЗАПРЕЩЁН — см. блок выше)
    python AstoryClient\Tools\asda_apply_name_overlay.py      # вернуть имена (слой-рецепт)
    ASDA_TABLES=item_list  run_headless_safe.py AstoryClient\Tools\_import_one_itemdb_table.py
  ★ Это НЕ дефект разбора: `item_list.bin` владельца (md5 794f83ad, skill_info.sif d412ea82)
    декодируется здесь верно — расхождение лежит в самих данных сборки Chronicles Interlude.
    Перекрёстная сверка 16.09.2026 по тем же 316 кодам: XYT — 316/316 «коробка (11,200/201)»,
    Arabic ASDA Story — 224/224 (остальных кодов в сборке нет), живой сервер `item_table.txt`
    (кол. 16/17) — 316/316. Одиночка здесь именно линия Chronicles Interlude
    (AsdaChroniclesInterlude / _MINE / AstoryClient\Tools\Translate\out_client — у всех трёх 0/316).
"""
import sys, os, struct, zipfile, csv, json, argparse

# ★ Шифр строк и C-строки — ОБЩИЙ модуль порта `asda_bintable` (волна 2 аудита дублей, D8): одна реализация
#   на все инструменты. append, а не insert(0): модули Tools порта не должны заслонять здешние.
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))   # Tools проекта
import asda_bintable as _BT
import asda_ledger as _LG   # ★ дедуп №2 В2 Т8: общий читатель ведомостей и общая семантика ячейки «было → стало»
# ★ 2026-11-01 (item_dedup, этап 2, решение владельца): карта «код-дубль → основной» и список колонок-ссылок — ОБЩИЙ
#   модуль порта `asda_item_dedup` (им же пользуются генераторы JSON/CSV и точечная правка боевых файлов).
import asda_item_dedup as _DD

DEFAULT_BBZ = r'@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\Character\Bin.bbz'
DEFAULT_OUT = r'@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\ItemDB'
# Ведомость серверного оверлея коробок (см. ★★★ в шапке): её наличие проверяется после
# регенерации item_list, чтобы 316 записей не потерялись молча.
OVERLAY_MANIFEST = r'@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\ItemDB\_server_union_overlay.csv'

# ---------------------------------------------------------------------------
# ★★★ 2026-09-25 (волна tablesA, план `Reports/tables_full_audit.md` §14). ВЕДОМОСТИ ОБЪЕДИНЕНИЯ.
#   ЗАЧЕМ. Раньше обогащение таблиц порта (серверные коробки item_list, 162 строки booster_list, цены
#   причёсок 28.08) жило ТОЛЬКО в CSV, а этот скрипт при любой перегенерации пишет CSV заново из Bin.bbz —
#   и правка молча пропадала. Так 31.08.2026 перегенерация `--only soulmateskill.ssi,hairtable.bin` стёрла
#   приведение цен причёсок к серверу (198 строк, `asda_hairtable_sync.py --apply` от 28.08).
#   Теперь обогащение лежит в ВЕДОМОСТЯХ рядом с реестром и накладывается на КАЖДОМ декоде:
#     • TAIL_LEDGER_DIR/<таблица>.csv  — записи, ДОПИСЫВАЕМЫЕ В ХВОСТ (ключ записи `tail_ledger` в реестре);
#     • VALUE_LEDGER_DIR/<таблица>.csv — точечные замены ЗНАЧЕНИЙ существующих записей (`value_ledger`).
#   Ведомости строит `Tools/build_itemdb_ledgers.py` (хвосты) и `asda_hairtable_sync.py --apply` (цены).
#   Ведомости лежат ВНЕ Content (иначе движок принимает .csv за импортируемую таблицу).
#   Выключить наложение (чистый декод 1:1 с архивом, нужен аудиту) — ключ `--no-ledgers`.
LEDGER_ROOT      = r'@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\ItemDB'
TAIL_LEDGER_DIR  = os.path.join(LEDGER_ROOT, '_tail_append')    # дозапись в хвост
VALUE_LEDGER_DIR = os.path.join(LEDGER_ROOT, '_value_overlay')  # замена значений (было → стало)
# Колонки ведомости значений: номер записи, колонка, значение в архиве, значение после, источник, почему.
VALUE_LEDGER_COLS = ['row', 'column', 'was', 'now', 'source', 'reason']

# ---------------------------------------------------------------------------
# Низкоуровневые дешифро-хелперы (мутируют bytearray записи ПОСЛЕ снятия MD5)
# ---------------------------------------------------------------------------
def sub_dw(b, off, c):
    v = (struct.unpack_from('<I', b, off)[0] - c) & 0xFFFFFFFF
    struct.pack_into('<I', b, off, v)

def sub_w(b, off, c):
    v = (struct.unpack_from('<H', b, off)[0] - c) & 0xFFFF
    struct.pack_into('<H', b, off, v)

def sub_b(b, off, c):
    b[off] = (b[off] - c) & 0xFF

def dec_str(b, off, maxlen):
    """Строка: снятие шифра «байт-1» — тело в `asda_bintable.decipher_str_inplace` (режим TERM_AUTO).

    ★★★ 2026-08-09, ИСПРАВЛЕНО ВЕЗДЕ. Буква «я» в шифре — это 0x00 (0xFF+1), а НАСТОЯЩИЙ
    терминатор строки в шифрованном виде — 0x01 (даёт 0x00 после снятия шифра). Внутри текста
    байт 0x01 встретиться не может по построению, поэтому терминатор ищется однозначно:

      * если в поле ЕСТЬ байт 0x01 — строка идёт до него (формат после патча длин, см.
        `Docs/Port/systems/RU_MERGE_ФАНТАЗИУМ.md` §5). Все 0x00 внутри — это буквы «я», а не конец;
      * если 0x01 в поле НЕТ — старый формат, конец по первому 0x00.

    Прежняя версия смотрела только ПОСЛЕДНИЙ байт поля. Если объявленная длина не совпадала
    с реальной (у `specialitem_tooltiplist` стояло 200 вместо 180 и последним оказывался нулевой
    хвост записи), проверка уходила в старую ветку и обрывала текст на первой же «я»:
    «Улучшенное зелье здоровь», «[Врем». Теперь длина поля на решение НЕ влияет.
    ★ D8 (14.09.2026): для fanta-таблиц (терминатор 0x7F) этот режим НЕ годится — там явный
      `asda_bintable.TERM_FANTA` (см. asda_title_sources). Заполнитель после 0x01 обнуляется.
    """
    _BT.decipher_str_inplace(b, off, maxlen, _BT.TERM_AUTO)


# ---------------------------------------------------------------------------
# Чтение колонок из (уже дешифрованной) записи
# ---------------------------------------------------------------------------
def rd(b, off, typ):
    if typ == 'i32':  return struct.unpack_from('<i', b, off)[0]
    if typ == 'u32':  return struct.unpack_from('<I', b, off)[0]
    if typ == 'i16':  return struct.unpack_from('<h', b, off)[0]
    if typ == 'u16':  return struct.unpack_from('<H', b, off)[0]
    if typ == 'i8':   return struct.unpack_from('<b', b, off)[0]
    if typ == 'u8':   return struct.unpack_from('<B', b, off)[0]
    if typ == 'f32':
        v = struct.unpack_from('<f', b, off)[0]
        return round(v, 6)
    if typ.startswith('s'):
        n = int(typ[1:])
        # C-строка до NUL в cp1251, не декодировалось — latin-1 с заменой (общий asda_bintable)
        return _BT.decode_text(_BT.cstr_bytes(b, off, n), 'cp1251').strip()
    raise ValueError(typ)


# ---------------------------------------------------------------------------
# ★ 2026-09-27 (доп.задача координатора по items_consistency_fix_REVIEW.md): часть строковых полей
#   некоторых таблиц хранит КОРЕЙСКИЙ/ЯПОНСКИЙ (а иногда и русский) в кодировке cp949 (UHC), а rd()
#   декодирует строки как cp1251 — получается кракозябра («±Ы·№…», «¬®¬С…», «Ў…»). БАЙТЫ уже верные,
#   ошибка только в декоде. Здесь возвращаем строку, перечитанную как cp949 (если это чисто получается).
#   `не выдумывать`: это не перевод, а чтение тех же байт в правильной кодировке.
_CYR_RE = None
def _has_cyr(s):
    global _CYR_RE
    if _CYR_RE is None:
        import re as _re
        # ★ 27.09.2026 (itemsdatafix, МЕЛК-3 ревизии items_data): весь блок кириллицы cp1251 (U+0400…U+045F),
        #   а не только А-Яа-яЁё. Байты cp949 0xA3/0xBD/0xBA читаются в cp1251 как «Ј/Ѕ/є» — строка «·ЈЅє»
        #   (= «랜스») раньше не распознавалась и оставалась кракозяброй (15 ячеек item_upgrade_cost.Name).
        _CYR_RE = _re.compile('[Ѐ-џ]')
    return bool(_CYR_RE.search(s))


def _is_cp949_suspect(s):
    """True, если в строке есть символ cp1251 из диапазона байт 0x80–0xBF (кроме Ё/ё) — верный признак
    того, что байты на деле cp949 (ведущий/ведомый байт), а не cp1251. У настоящего русского текста
    (буквы 0xC0–0xFF) таких символов нет; русская пунктуация «»№—… тоже сюда попадает, поэтому этот
    признак применяем ТОЛЬКО к смешанным таблицам, а к чисто-cp949 столбцам перекодируем сплошь."""
    for c in s:
        if c in 'Ёё':
            continue
        try:
            e = c.encode('cp1251')
        except Exception:
            continue
        if len(e) == 1 and 0x80 <= e[0] <= 0xBF:
            return True
    return False


def _reread_cp949(s):
    """Строку, ошибочно прочитанную как cp1251, перечитать как cp949. Вернуть None, если не выходит
    чисто (нечётные/неопределённые байты — U+FFFD): тогда исходная строка остаётся как есть."""
    if not s:
        return None
    try:
        r = s.encode('cp1251').decode('cp949')
    except Exception:
        return None
    return None if '�' in r else r


def fix_cp949_field(s, force):
    """force=True (чисто-cp949 столбец, реального cp1251 в нём нет): перекодировать любую строку с
    кириллицей, если cp949 читается чисто. force=False (смешанный столбец): только если есть признак
    cp949 (0x80–0xBF), чтобы не тронуть настоящий русский текст."""
    if not s or not _has_cyr(s):
        return s
    if not force and not _is_cp949_suspect(s):
        return s
    r = _reread_cp949(s)
    return r if r is not None else s

# ---------------------------------------------------------------------------
# Генераторы колонок для числовых массив-таблиц (lossless: каждый dword -> столбец)
# ---------------------------------------------------------------------------
def gen_ints(n, base=0, names=None, typ='i32'):
    cols = []
    for k in range(n):
        nm = None
        if names and k < len(names) and names[k]:
            nm = names[k]
        cols.append((base + k*4, typ, nm or f'dw{k:02d}'))
    return cols

# ---------------------------------------------------------------------------
# Дешифро-замыкания шифрованных таблиц (b = bytearray данных после MD5; i = индекс)
# ---------------------------------------------------------------------------
# --- item_list (индексная схема, эталон CItemManager_LoadItemList_BIN) ---
IL_DW = {0,17,19,20,21,22,23,24,25,27,28,30,31,32,33,35,36,37,38,39,
         40,41,42,43,45,46,48,49,50,51,53,57,58,54,56,59}
IL_W  = {36,52,58,68,69,88,94,104,110}
def dec_item_list(b, i):
    for d in IL_DW: sub_dw(b, d*4, 35)
    for w in IL_W:  sub_w(b, w*2, 35)
    dec_str(b, 4, 64)
    # поля 60/61 (byte 240/244) НЕ из файла — перезаписываются индексом записи
    struct.pack_into('<I', b, 240, i & 0xFFFFFFFF)
    struct.pack_into('<I', b, 244, i & 0xFFFFFFFF)

def dec_item_tooltip(b, i):
    # ★★★ 2026-10-14 (names11, обрывы подсказок). Поле Description — char[200] @4 (колонка `s200`, запись 220 б = 16 MD5 +
    #   4 id + 200 текст). Здесь стояло 164: шифр снимался только с первых 164 байт, остальные 36 оставались зашифрованными
    #   (+1). Следствия: (1) русские подсказки длиннее 164 б обрывались («…чашу с д», «Эффект длитс», «…можно получить»);
    #   (2) в старом формате (0x01 в поле нет) строка резалась на первом 0x00 — а это буква «я» (0xFF+1), отсюда обрывы
    #   на «я»: «дл», «веро», «позвол»; (3) у английских записей кодов 78411… тянулся мусор «see uifjs!tvo!sjtf!bhbjo/».
    #   С 200 терминатор 0x01 находится у всех записей, «я» не режет строку; 650 ячеек Description стали полными, прочие колонки
    #   и 58 228 остальных подсказок байт в байт прежние (сверка 14.10 в wNAMES11_logs/trunc).
    #   [ОРИГИНАЛ] Эталон `CItemManager_LoadItemTooltip@0xEC74D0` (`RE/etalon/item_inventory.c`): запись 220 б = 16 б MD5 + 204 б
    #   (id i32 с шифром -30 и текст char[200]); шифр с текста эталон снимает циклом по `strlen` ШИФРОВАННОГО буфера, то есть сам
    #   режет строку на первом «я» (0x00 в шифре) - это дефект оригинала. [ПОРТ] Мы исправляем его (закон fix-original-defects):
    #   терминатором считается 0x01 (заполнитель хвоста поля), «я» внутри текста сохраняется. Ширина поля 200 = ширине эталона.
    sub_dw(b, 0, 30); dec_str(b, 4, 200)

def dec_special_itemlist(b, i):
    sub_dw(b,0,32); sub_w(b,68,32); sub_dw(b,72,32); sub_dw(b,76,32); sub_dw(b,80,32)
    sub_w(b,84,32); sub_b(b,86,32); sub_b(b,87,32)
    sub_w(b,88,33); sub_dw(b,92,33); sub_dw(b,96,33)
    for o in range(100,107): sub_b(b,o,33)
    sub_dw(b,108,33); sub_dw(b,112,33); sub_b(b,116,33); sub_b(b,117,33)
    sub_dw(b,120,33); sub_b(b,124,33); sub_dw(b,128,33)
    sub_b(b,196,34); sub_b(b,229,34); sub_b(b,294,33); sub_b(b,295,33); sub_b(b,296,33)
    # ★★★ 2026-08-09. Длины строковых полей — ДО СЛЕДУЮЩЕГО ПОЛЯ, а не «до конца записи».
    #   Раньше стояло `300-o`, то есть имя объявлялось длиной 296 байт — на весь остаток записи.
    #   Со старым `dec_str` это сходило с рук (он останавливался на первом 0x00), но новый ищет
    #   истинный терминатор 0x01, а в уже дешифрованных ЧИСЛОВЫХ полях байт 0x01 встречается
    #   сплошь и рядом ⇒ терминатор находился ложно, и хвост записи обнулялся целиком.
    #   Симптом: `special_itemlist` = имена на месте, ВСЕ остальные колонки и `DataHex` в нулях.
    #   Границы выведены из смещений СОСЕДНИХ числовых полей этой же функции:
    #     имя 4…67 (64 б, дальше `sub_w(68)`) · 132…163 (32, дальше строка 164) · 164…195
    #     (32, дальше `sub_b(196)`) · 197…228 (32, дальше `sub_b(229)`) · 230…261 (32) ·
    #     262…293 (32, дальше `sub_b(294/295/296)`).
    for o, ln in ((4,64), (132,32), (164,32), (197,32), (230,32), (262,32)):
        dec_str(b, o, ln)

def dec_special_tooltip(b, i):
    # ★★★ 2026-08-09. Поле подсказки — char[180] @36 (колонка `s180`), а НЕ 200 байт: запись 236 б,
    #   и последние 20 байт (216…235) — нулевой хвост записи, а не часть строки. Из-за maxlen=200
    #   проверка «заполнитель 0x01» в `dec_str` смотрела на байт 235 (там 0x00) и ВСЕГДА уходила в
    #   ветку «дешифровать до первого нуля». В шифре буква «я» = 0x00 (0xFF+1), поэтому 288 из 558
    #   подсказок обрывались на первой же «я»: «Улучшенное зелье здоровь», «[Врем», «… к кузнецу). Дл».
    #   Эталонный загрузчик `CItemManager_LoadSpecialItemTooltip@0xEC6A00` (item_inventory.c:63710)
    #   делает `strlen` по ШИФРОВАННОМУ буферу и потому обрывается там же — это ДЕФЕКТ ОРИГИНАЛА
    #   (закон `fix-original-defects`): правильный заполнитель поля здесь 0x01, и при maxlen=180
    #   дешифруется всё поле целиком, включая «я».
    sub_dw(b,0,36); dec_str(b,4,32); dec_str(b,36,180)
    # ★★★ Эталон `CItemManager_LoadSpecialItemTooltipBin` (item_inventory.c:63721): сразу после
    #   цикла декодирования id ПЕРВОЙ записи принудительно перезаписывается единицей
    #   (`**(_DWORD **)... = 1;`), и уже он идёт в `g_SpecialItemTooltipFirstId`.
    #   Без этого первая запись получает id 0, а коды спец-предметов начинаются с 1 —
    #   подсказка первого предмета становится недостижимой (проверено: в нашем CSV было `0,0,…`).
    if i == 0:
        struct.pack_into('<I', b, 0, 1)

def dec_sowell_list(b, i):
    sub_dw(b,0,16); dec_str(b,4,32)
    for o in (36,37,38,39): sub_b(b,o,16)
    sub_w(b,40,21); sub_w(b,42,21)
    for o in (44,45,46,47): sub_b(b,o,21)
    sub_dw(b,48,21); sub_dw(b,52,21); sub_dw(b,56,26); sub_dw(b,60,26)
    for o in (64,65,66): sub_b(b,o,26)
    sub_dw(b,68,26); sub_b(b,72,26)
    dec_str(b,73,32); dec_str(b,105,32); dec_str(b,137,32)
    sub_b(b,169,26); sub_b(b,170,26)

def dec_sowell_tooltip(b, i):
    sub_dw(b,0,33); dec_str(b,4,200)

def dec_title_list(b, i):
    for o in (0,4,8,12): sub_dw(b,o,40)
    dec_str(b,16,64)
    for o in (80,84,88,92,96): sub_dw(b,o,40)
    dec_str(b,100,64)
    for o in (164,168,172): sub_dw(b,o,40)
    for k in range(6): sub_dw(b,176+4*k,40)
    sub_dw(b,200,40)
    for k in range(6): sub_dw(b,204+4*k,40)
    for o in (228,232,236): sub_dw(b,o,40)

def dec_title_effect(b, i):
    sub_dw(b,0,46)
    for o in (4,36,68,100,132,164,196,228): dec_str(b,o,32)

def dec_title_option(b, i):
    sub_dw(b,0,43); dec_str(b,4,32)

def dec_title_rank(b, i):
    for o in (0,4,8,12,16): sub_dw(b,o,49)
    dec_str(b,20,32)
    for o in (52,56,60,64): sub_dw(b,o,49)

# ---------------------------------------------------------------------------
# РЕЕСТР ТАБЛИЦ
#   entry     : имя записи в ZIP (lower)
#   stride    : disk-stride
#   md5       : байт MD5-префикса (0 если нет)
#   header    : есть ли 20-байтовый header (default True)
#   fixed     : (fixed_bytes, unit) — таблица читается фикс. блоком, count игнор (refineupmoney)
#   noheader  : True -> count = usize/stride, header отсутствует (.ssi/.sif)
#   dec       : функция дешифровки (b,i)->None либо None
#   cols      : список (off, typ, name)
#   dump_hex  : добавить _data_hex (полная запись; неполная раскладка/потенц. скрытые поля)
#   src_bbz   : ★ путь к ДРУГОМУ Bin.bbz именно для этой таблицы (общий --bbz не меняется).
#               Только для случая «в основной сборке файл вырожден, формат в годной тот же»;
#               обоснование обязано стоять комментарием у самой записи.
#   note      : пометка статуса
# ---------------------------------------------------------------------------
def C(*t): return list(t)

# ---------------------------------------------------------------------------
# ★★★ ИСПРАВЛЕНИЯ ДЕФЕКТОВ ОРИГИНАЛА ПО КОДУ ПРЕДМЕТА (закон `fix-original-defects`)
# ---------------------------------------------------------------------------
#   Запись i плотных таблиц item_list / item_img_table = код 20551+i
#   (`CItemManager_GetItemRecord@0x9C3CE0`: itemTable + stride*(code − 20551)).
#   Форма: { имя записи архива: { itemCode: { колонка: ('было', 'стало'), ..., 'reason': '...' } } }.
#   Применяется в decode_table() НА БАЙТАХ записи ДО разбора колонок, поэтому колонки и DataHex
#   остаются согласованы (в DataTable уходит та же запись, что и в CSV). Правила те же, что у
#   `text_fixes`: если в данных лежит НЕ 'было' — правка НЕ накладывается, печатается предупреждение.
#
#   ★ 2026-09-06 «Diving Suit» (набор мешей 310, слоты верх/перчатки/штаны/обувь, 3 цвета):
#     в ОБОИХ клиентских item_img_table.bin (tables_orig и Фантазиум, байт-в-байт) женские
#     колонки MeshFemale/TexFemale у 41827–41830, 41837–41840, 41847–41850 скопированы с первой
#     записи набора (шлем `ch1_f_00_hcap310_00_00_N`) — на женском персонаже все четыре части
#     рисуются шапкой. Доказательство: подиффовать запись с её дублем +5 («Stripe/Yellow/Dot
#     Pattern Bikini», 41832–41835/41842–41845/41852–41855): отличаются ТОЛЬКО id и подстрока
#     `hcap`→`chst/grov/pant/shoe` в двух женских полях; мужские колонки и дубли верны.
#     Значения «стало» = женские колонки дубля (не выдуманы; glb/uasset для всех есть в
#     RawAssets/Models/Char и Content/AsdaAssets/Models/Char). Ревизия:
#     `Reports/render_sets_2026-09-05/review_rename_and_fog.md` §3, решение владельца
#     06.09.2026 «исправляем»; отчёт `Reports/render_sets_2026-09-05/itemdb_310_fix_report.md`.
ITEM_CODE_BASE = 20551

def _fix310(code, part, color):
    """Одна запись набора 310: женские меш/текстура = как у дубля code+5 (см. комментарий выше)."""
    return {
        'MeshFemale': ('ch1_f_00_hcap310_00_00_0',          f'ch1_f_00_{part}310_00_00_0'),
        'TexFemale':  (f'ch1_f_00_hcap310_00_00_{color}',   f'ch1_f_00_{part}310_00_00_{color}'),
        'reason': f'дефект оригинала: женские колонки набора 310 (Diving Suit, цвет {color}) '
                  f'скопированы со шлема; значения = дубль {code+5}',
    }

ORIGINAL_DEFECT_FIXES = {
 'item_img_table.bin': {
    41827: _fix310(41827, 'chst', 0), 41828: _fix310(41828, 'grov', 0),
    41829: _fix310(41829, 'pant', 0), 41830: _fix310(41830, 'shoe', 0),
    41837: _fix310(41837, 'chst', 1), 41838: _fix310(41838, 'grov', 1),
    41839: _fix310(41839, 'pant', 1), 41840: _fix310(41840, 'shoe', 1),
    41847: _fix310(41847, 'chst', 2), 41848: _fix310(41848, 'grov', 2),
    41849: _fix310(41849, 'pant', 2), 41850: _fix310(41850, 'shoe', 2),
 },
}

def expand_defect_fixes(name):
    """ORIGINAL_DEFECT_FIXES[name] -> список кортежей формы text_fixes (строка, колонка, было, стало, почему)."""
    out = []
    for code, fields in ORIGINAL_DEFECT_FIXES.get(name, {}).items():
        why = fields.get('reason', 'исправление дефекта оригинала')
        for col, val in fields.items():
            if col == 'reason': continue
            old, new = val
            out.append((code - ITEM_CODE_BASE, col, old, new, f'код {code}: {why}'))
    return out

REGISTRY = {
 # === item_table_format.md ===
 # ★★★ 2026-09-25 (миграция оверлея): 316 предметов-коробок, которым сервер `item_table.txt` проставляет
 #   ItemType/ItemSubType/DurationOrEnchant, — теперь в ведомости значений `_value_overlay/item_list.csv`
 #   (её строит `Tools/build_itemdb_ledgers.py --values`), а не в отдельном `port_itemdb_union_from_server.py`,
 #   правившем CSV после декода (пропадал при перегенерации, как цены причёсок 31.08). Обоснование —
 #   `Reports/server_package_tables_sync.md` §4.1. Восстановленные имена по-прежнему шаг 2 (см. шапку).
 # ★ 2026-10-24 (fable_shop): +79 премиум-коробок маунтов донора 79429…79507 (ItemType 11 / SubType 200, пакет = маунт
 #   559…637) в ХВОСТ — ведомость `_tail_append/item_list.csv` строит `AstoryClient/Tools/asda_fable_mounts_boxes.py`.
 #   Без коробки шлюз GP7 не может выдать функциональный маунт: GameServer принимает в ответе только код ≥ 20551.
 'item_list.bin': dict(stride=264, md5=16, dec=dec_item_list, dump_hex=True, value_ledger='item_list.csv',
    tail_ledger='item_list.csv', tail_key='ItemCode', cols=C(
    (0,'i32','ItemCode'),(4,'s64','ItemName'),(76,'i32','EnchantOrSocket'),
    (80,'i32','RequiredLevel'),(84,'i32','RequiredLevelMax'),(88,'i32','ClassRestriction'),
    (92,'i32','UsageCategory'),(96,'i32','LinkCode'),(100,'i32','LinkedGroupCode'),
    (128,'i32','ItemType'),(132,'i32','ItemSubType'),(140,'i32','AttackRangeTiles'),
    (144,'i32','ItemVariant'),
    (152,'i32','TradableFlag'),(156,'i32','CategoryFlag'),(160,'i32','SubCategory'),
    (164,'i32','Price'),(176,'i16','FlagWord176'),(200,'i32','TooltipFlag200'),
    (216,'i32','DurationOrEnchant'),(220,'i16','LinkWordCode'),(228,'i32','GroupCode'),
    (232,'i32','SetCode'),(236,'i32','ItemGrade'),(240,'i32','ImgIndex')),
    # ★ 2026-09-25 (сверка свитков заточки, `Reports/gear_upgrade/scroll_sync.md`). Поля записи без
    #   колонки CSV, которые адресует ведомость значений (патч ложится на байты -> DataHex). Соответствие
    #   серверному `item_table.txt` снято сверкой всех 58 878 общих кодов: +72 = токен 2 (вес единицы,
    #   97.6 %; рантайм читает `LOWORD(field72)`, `AsdaItemTableSubsystem.h` Weight), +184 = токен 31
    #   (99.5 %), +196 = токен 34 (99.5 %).
    # ★ 2026-09-25 (Д-9, ревизия С2, `Reports/gear_upgrade/d9_rev_apply.md`). OptionGroup0/1 = u16 @0x68/@0x74 —
    #   группа возможных доп-опций гнезда 0/1 (окно 50300, `AsdaItemTableSubsystem.cpp` U16(0x68)/U16(0x74)).
    #   Серверный аналог — item_table.txt ток.10 (+18) / ток.13 (+22): их берут переброска
    #   `Recv_UserItemOptionTrans@0x536460` и генераторы опций при создании вещи (поправлено 2026-09-25,
    #   `Reports/gear_upgrade/optgroup_sync.md`; SowelCombine col49/col50 — другая величина, заточка +6/UpperRefine).
    #   −1 сервера = 0xFFFF клиента; 0 = группа без опции (сервер выдаёт −1).
    # ★ 2026-09-27 (приглашения 23762/23763, `Reports/owner_0927/items_consistency.md`). Поля записи без
    #   колонки CSV, в которых запись Chronicles/_MINE — клон оружейной записи, а сервер, XYT, Arabic и
    #   Фантазиум единогласно дают другую запись: +68 (прочность макс.), +136, +168, +172, +180, +188, +192.
    #   Серверные токены (карта tokmap сверки свитков): +168=ток.27, +172=ток.28, +180=ток.30, +188=ток.32,
    #   +192=ток.33; +68/+136 — по трём сборкам клиента. Только адресация ведомости, смысл полей не вводится.
    ledger_fields=dict(Weight=(72, 'i32'), Field184=(184, 'i32'), Field196=(196, 'i32'),
                       OptionGroup0=(104, 'u16'), OptionGroup1=(116, 'u16'),
                       Field68=(68, 'i32'), Field136=(136, 'i32'), Field168=(168, 'i32'),
                       Field172=(172, 'i32'), Field180=(180, 'i32'), Field188=(188, 'i32'),
                       Field192=(192, 'i32'))),
 # 2026-08-01: добавлены поля, которые читал рантайм-порт напрямую из .bin, но которых
 # не было в CSV/DataTable — иконки-атласы (ячейка), звук дропа, дальность атаки оружия.
 #   item_list  +0x8C AttackRangeTiles (эталон Char equip case 18: (v*100)^2 в поле игрока +0x2EA10)
 #   item_img   +0x84/+0xA4/+0xA8/+0xAC/+0xCC иконка-атлас+ячейка (uiInventory_DrawItemOptionIcons),
 #              +0xD0 DropSoundCat (loot_system_re.md §3, 0..11 -> Sod_drop_*)
 'item_img_table.bin': dict(stride=300, md5=0, dec=None, dump_hex=True, cols=C(
    (4,'s32','MeshMale'),(36,'s32','TexMale'),(68,'s32','MeshFemale'),(100,'s32','TexFemale'),
    (132,'s32','IconAtlasMale'),(164,'i32','IconCellMale'),(168,'i32','IconSmallFlag'),
    (172,'s32','IconAtlasFemale'),(204,'i32','IconCellFemale'),
    (208,'i32','DropSoundCat'),
    (212,'i32','ImgSubType'),(288,'i32','SubTypeMale'),(292,'i32','SubTypeFemale')),
    # ★ 2026-09-25 (сверка свитков заточки): иконки 20952/20954 идут вслед за сменой вида свитка
    #   (20952 -> вид 74, как его серверный близнец 20936; 20954 -> вид 91 «100 %»). `scroll_sync.md`.
    value_ledger='item_img_table.csv',
    # ★ 2026-10-24 (fable_shop): +79 строк иконок премиум-коробок маунтов 79429…79507 (строка img = строка item_list).
    #   Ведомость строит `AstoryClient/Tools/asda_fable_mounts_boxes.py`; ключа нет — позиционная таблица.
    tail_ledger='item_img_table.csv'),

 # === group A ===
 'item_tooltip.bin': dict(stride=220, md5=16, dec=dec_item_tooltip,
    cols=C((0,'i32','ItemCode'),(4,'s200','Description')),
    # ★ 2026-09-25 (А4): +5 описаний кодов 56054/56059/56069/56074/56079 в ХВОСТ (строки 58878…58882).
    #   В архиве на их ПОЗИЦИЯХ (код−20551) лежат записи с чужими кодами 56311/56316/56326/56331/56336
    #   и текстом «-» (дефект оригинала; эталон `itemTemplateFindByCode@0xB733F0` берёт запись по
    #   позиции и показывает «-»). Порт ищет по КЛЮЧУ ItemCode (`BuildKeyIndex`), поэтому дозапись
    #   в хвост даёт им описание, не трогая существующие строки. Текст — серверный `item_tooltip.txt`
    #   (перевод `Reports/translations_optiontable_tooltip.md` §2).
    tail_ledger='item_tooltip.csv', tail_key='ItemCode',
    # ★ 2026-09-25 (сверка свитков заточки): числа в описаниях свитков приведены к серверному
    #   `ItemUpgrade.txt` (шанс/улучшение/уровни по виду свитка из `item_table.txt` +27). `scroll_sync.md`.
    value_ledger='item_tooltip.csv',
    note='запись = ItemCode + Description char[200]; числового хвоста нет (было заужено s164)'),
 'item_upgrade_cost.bin': dict(stride=72, md5=0, dec=None,
    cols=C((0,'s64','Name'),(64,'i32','ItemCode'),(68,'i32','Value')),
    # ★ 2026-09-25 (А3): +37 619 кодов из серверного `SowelCombine.txt` (Value = серверная col1) в ХВОСТ.
    #   Адресация КЛЮЧЕВАЯ: единственный потребитель эталона `recordTableLookupValueByKey@0xE21860`
    #   (bin_tables.c:368) ищет запись ЛИНЕЙНО по полю +64 (ItemCode) и берёт +68 (Value); ненайденный
    #   код даёт −1 (`uiStatBar_UpdateValueText@0xE20310` умножает −1 на ставку). Позиция записи не
    #   важна, Name (+0) не читается. Name у дописанных записей ПУСТОЕ: у сервера имени нет.
    tail_ledger='item_upgrade_cost.csv', tail_key='ItemCode',
    cp949_cols=('Name',),  # ★ 2026-09-27: Name — cp949 корейское имя материала (реестром помечено), не cp1251
    note='Name=char[64] (CP949 корейское имя материала — БЫЛ порезан на 16 int); ItemCode/Value HIGH'),
 'item_upgrade_price.bin': dict(stride=8, md5=0, dec=None,
    cols=C((0,'i32','Index'),(4,'i32','Value'))),
 # ★ 2026-09-25 (opt105-pct): ведомость значений — RU-шаблоны 309…315 потеряли «%%» («+%d» вместо «+%d%%»,
 #   английский Interlude «+%d%%», сервер считает процент: GetPrimiumItemOptionValue, клиент делит на 100).
 #   Разбор: `Reports/gear_upgrade/opt105_pct_fix.md`. Правка ложится на байты записи при каждом декоде.
 'itemoptiontable.bin': dict(stride=404, md5=0, dec=None, value_ledger='itemoptiontable.csv',
    cols=C((0,'u32','OptionId'),(4,'s400','FmtString')),
    note='2 поля HIGH: OptionId + FmtString printf-шаблон (БЫЛ ошибочно 101 dword — это буфер строки)'),
 'itemoptionvalue.bin': dict(stride=164, md5=0, dec=None,
    cols=C((0,'u32','Id'))+[(4+8*k, 'u32', f'Level{k:02d}') for k in range(20)]
                          +[(8+8*k, 'u32', f'Value{k:02d}') for k in range(20)],
    note='LoadItemOptionValue2 (packed в Bin.bbz): id + 20 пар (Level=ступень, Value=значение опции) MED'),
 # loose-файл (НЕ в Bin.bbz) — диапазоны значений опций предметов (ItemOptionValue.BIN)
 'itemoptionrange.bin': dict(stride=20, md5=0, dec=None,
    loose=r'@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\ItemOptionValue.BIN',
    cols=C((0,'i32','RowId'),(4,'i32','GroupId'),(8,'i32','OptionType'),(12,'i32','MinValue'),(16,'i32','MaxValue')),
    note='loose IPV (ItemOptionValue.BIN): OptionType=ItemOptionTable.OptionId; Min/MaxValue=сырой диапазон стата (сервер роллит) — всё HIGH'),
 # loose-файл (НЕ в Bin.bbz) — базовый диапазон стата shell-предмета по УРОВНЮ персонажа.
 # Эталон `PremiumLevelTable_Load@0xD34100`: fopen("./Data/PremiumLevelTable.bin"),
 #   header 20 б (count в +4) + count × 28 б (operator new[](28*count), поле this[1105]).
 # Раскладка полей — из ЕДИНСТВЕННОГО консьюмера `inventoryFindMatchingStackSlot@0xD34260`:
 #   [+0]=Level, [+4]=ItemGrade, [+8]=ItemType, [+12]=UsageCategory, [+16]=ItemSubType,
 #   и [+20]/[+24], которые тултип-панель кладёт в строку 2360 «Эффект ЖК : min ~ max»
 #   (item_inventory.c:37284-37289) вместо баз `ItemRecord->gap_88`.
 'premiumleveltable.bin': dict(stride=28, md5=0, dec=None,
    loose=r'@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\PremiumLevelTable.bin',
    cols=C((0,'i32','Level'),(4,'i32','ItemGrade'),(8,'i32','ItemType'),(12,'i32','UsageCategory'),
           (16,'i32','ItemSubType'),(20,'i32','StatMin'),(24,'i32','StatMax')),
    note='loose PremiumLevelTable.bin: ключ = (Level, ItemGrade, ItemType, UsageCategory|ItemSubType), значение = StatMin/StatMax (диапазон «Эффект ЖК» shell-предмета) — всё HIGH (подтверждено единственным консьюмером)'),
 # loose-файл (НЕ в Bin.bbz) — РЕЦЕПТЫ КРАФТА (окно «Изготовление», CItemMainWnd).
 # Эталон `tableLoad128ByteRecords@0xFDDE40` (bin_tables.c:415) из `CItemMainWnd_LoadDataAndInit@0xFDD630`
 #   (item_inventory.c:69514): fopen("./Data/NPC/making_recipe.BIN"), header 20 б (count в +4),
 #   затем count × 128 б (`operator new[](count << 7)`, поля this[6]=count / this[8]=массив).
 #   В нашем файле 249 записей: 20 + 249×128 = 31 892 б = размер файла.
 # Раскладка полей — по КОНСЬЮМЕРАМ эталона (каждое поле подтверждено чтением в коде):
 #   +0   i16  Id           — `findRecordById16@0xFDEDA0` сравнивает `*(i16*)(rec)` с искомым id;
 #                            в файле Id == порядковому номеру записи (проверено на всех 249).
 #   +2   char[62] RecipeName — `CControl_SetText(..., rec + 2)` (ui_core.c:100801, item_enhance.c:10108);
 #                            фактическая длина имён ≤ 33 б, поле упирается в нулевые +64/+65.
 #                            ⚠ Колонка НЕ может называться `Name`: в JSON-выгрузке `write_outputs`
 #                            под этим именем идёт ИМЯ СТРОКИ (= `_row`), и колонка его затирала бы
 #                            (у `item_upgrade_cost` это уже так — повторять не стали).
 #   +66  u8   Category     — фильтр вкладок `uiBuildFilteredItemPage@0xFE0A20` (`rec+66 == вкладка`)
 #                            и подпись категории `Loc(5540 + rec[66])` (ui_core.c:100799).
 #   +67  u8   CraftLevel   — требуемый уровень изготовления (`rec+67 > уровень игрока` → рецепт серый).
 #   +68/+69 u8 Unk68/Unk69 — КЛИЕНТ НЕ ЧИТАЕТ (сплошной поиск по декомпилу консьюмеров не дал
 #                            ни одного обращения). Сохраняем как есть, чтобы не терять данные.
 #   +72  i32  ResultCode   — код ИЗГОТАВЛИВАЕМОГО предмета (`CItemManager_GetItemRecord(rec+72)`).
 #   +76  i32×6 Mat0..Mat5  — коды СЫРЬЯ (−1 = слот пуст); `sub_FE1040@0xFE1040` перебирает ровно
 #                            эти шесть полей → предикат «предмет является сырьём» (категория 7
 #                            автосортировки инвентаря).
 #   +100 i16×6 MatCount0.. — требуемое количество каждого сырья (`craftCheckRequiredItems@0xFE0CF0`
 #                            сравнивает с `CInventory_CountItemById`).
 #   +112 i16×6 SubRecipe0..— id рецепта, которым сырьё можно изготовить самому (кнопка «+»);
 #                            значение уходит в `findRecordById16` (item_enhance.c:10172). −1 = нет.
 #   +124 i16  IdCopy       — дубль Id (у пустой записи 0 там −1, как и во всех её полях), +126 = 0.
 'making_recipe.bin': dict(stride=128, md5=0, dec=None,
    loose=r'@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\NPC\making_recipe.BIN',
    cols=C((0,'i16','Id'),(2,'s62','RecipeName'),(64,'i16','Pad64'),(66,'u8','Category'),
           (67,'u8','CraftLevel'),(68,'u8','Unk68'),(69,'u8','Unk69'),(70,'i16','Pad70'),
           (72,'i32','ResultCode'))
        +[(76 + 4*k, 'i32', f'Mat{k}')        for k in range(6)]
        +[(100 + 2*k, 'i16', f'MatCount{k}')  for k in range(6)]
        +[(112 + 2*k, 'i16', f'SubRecipe{k}') for k in range(6)]
        +C((124,'i16','IdCopy'),(126,'i16','Pad126')),
    # ★ 2026-09-25 (ремесло, волна В0, `Reports/craft/craft_re_report.md` §7, ревизия Д-Д1): ведомость
    #   значений — рецепты 222…228 «(благослов.)» в клиенте показывают ОБЫЧНЫЙ посох (+72 = 71927…72525),
    #   а сервер выдаёт благословенный (`CraftRecipeMake.txt` кол.3 = 72750…72776). Авторитет — сервер
    #   (закон `client-server-table-desync`), правка — дефект данных оригинала, только через ведомость.
    #   ⚠ `IdCopy` (+124) на деле **MakeIndex** — ключ записи making_rate и серверного CraftRecipeMake
    #   (консьюмер `uiItemUpgradeWindowOpen` ui_core.c:101753 → `shopFindRecordByItemId`). Колонку НЕ
    #   переименовываем в этой волне: имя уходит в генерируемый USTRUCT и DT (переименование = пересборка
    #   схемы DataTable); код порта читает её как MakeIndex с пометкой.
    value_ledger='making_recipe.csv',
    note='loose NPC/making_recipe.BIN: рецепты крафта. Mat0..Mat5 = ЕДИНСТВЕННЫЙ источник списка сырья '
         '(категория 7 автосортировки инвентаря). Pad64/Pad70/Pad126 в файле всегда 0, Unk68/Unk69 клиент не читает'),
 # loose-файл (НЕ в Bin.bbz) — ИСХОДЫ СТУПЕНЕЙ РЕМЕСЛА (окно изготовления 77500, CItemQtyMakeDlg).
 # ★ 2026-09-25 (ремесло, волна В0). Эталон `tableLoad52ByteRecords@0xFDE0C0` (bin_tables.c:467) из
 #   `CItemMainWnd_LoadDataAndInit@0xFDD630` (item_inventory.c:69518): fopen("./Data/NPC/making_rate.BIN"),
 #   header 20 б (count в +4, магия `ICR` в +12, `strncmp(off_147AB38, …, 4)` bin_tables.c:488), затем
 #   count × 52 б. В файле 249 записей: 20 + 249×52 = 12 968 б = размер файла; md5 9f0aff83 одинаков во всех
 #   четырёх Interlude-клиентах (`Reports/craft/craft_data.md` §0). Без этой таблицы окно 77500 НЕ
 #   ОТКРЫВАЕТСЯ: `uiItemUpgradeWindowOpen` возвращает 0, если запись по MakeIndex рецепта не найдена.
 # Раскладка — по консьюмерам эталона (инициализатор `craftResetSlots` item_enhance.c:10005):
 #   +0  u16  MakeId          — ключ поиска `shopFindRecordByItemId@0xFDEE30` (shop_trade.c:22227); = индекс;
 #   +2  u8   Flag2           — = серверный CraftRecipeMake кол.1 (0 у 16 рецептов, 0xFF у прочих); ссылок в срезе нет;
 #   +3  u8   Pad3            — 0;
 #   +4  i32  BonusQtyChance  — = CraftRecipeMake кол.2 (шанс «+1 шт.» из 100000; 3000 у 16, иначе −1);
 #                              тик окна читает `rate+2 == 0` как признак бонуса (item_enhance.c тик ступеней);
 #   +8  i32×7 OutItem0..6    — исходы по ступеням качества: `craftCountActiveSlots` (item_enhance.c:10227)
 #                              и иконки ступеней `uiUpgradeSlotsRedraw` (ui_core.c:101816-101819);
 #   +36 i16×7 OutQty0..6     — количества исходов (= CraftRecipeMake кол.10…16); ссылок в срезе нет;
 #   +50 i16  Pad50           — 0.
 'making_rate.bin': dict(stride=52, md5=0, dec=None,
    loose=r'@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\NPC\making_rate.BIN',
    cols=C((0,'u16','MakeId'),(2,'u8','Flag2'),(3,'u8','Pad3'),(4,'i32','BonusQtyChance'))
        +[(8 + 4*k, 'i32', f'OutItem{k}') for k in range(7)]
        +[(36 + 2*k, 'i16', f'OutQty{k}') for k in range(7)]
        +C((50,'i16','Pad50'),),
    note='loose NPC/making_rate.BIN: исходы ступеней ремесла по MakeIndex рецепта (making_recipe +124). '
         'OutItem0..6 — иконки ступеней окна 77500; без записи окно изготовления не открывается'),
 'itemchangegrade.bin': dict(stride=8, md5=0, dec=None,
    cols=C((0,'i32','Grade'),(4,'i32','Cost'))),
 'itemopgraft.bin': dict(stride=12, md5=0, dec=None,
    cols=C((0,'i32','LevelLow'),(4,'i32','LevelHigh'),(8,'i32','Value'))),
 'special_itemlist.bin': dict(stride=316, md5=16, dec=dec_special_itemlist, dump_hex=True,
    # ★ 2026-10-13 (wave_DATA, P65): ведомость значений — коды 367/393 = серверная линия (Function_Table тип 17/15),
    #   а не «свиток обмена опций» RU/Interlude (`Reports/anticheat/wave_DATA_20261013.md` §1).
    value_ledger='special_itemlist.csv',
    # ★ 2026-10-23: хвост — 79 маунтов донора AsdaFableClassic (коды 559…637, строки 558…636); ведомость строит
    #   AstoryClient/Tools/asda_fable_mounts_data.py (отчёт Reports/fable_mounts/FABLE_MOUNTS.md).
    tail_ledger='special_itemlist.csv', tail_key='Code',
    cols=C((0,'i32','Code'),(4,'s32','Name'),(68,'u16','RequiredLevel'),(72,'i32','ItemLevel'),(76,'i32','Field76'),
           (80,'i32','SubType'),(84,'u16','ResultModel'),(88,'u16','Field88'),(92,'i32','Field92'),(96,'i32','CooldownBase'),
           (101,'u8','Flag101'),(102,'u8','Flag102'),(103,'u8','Type103'),(108,'i32','Value108'),(112,'i32','Price'),
           (117,'u8','LinkGroupId'),(120,'i32','Field120'),(124,'u8','Flag124'),(128,'i32','Field128'),(132,'s32','TexEntry132'),
           (164,'s32','IconPath'),(196,'u8','IconCell'),(197,'s32','Field197'),(230,'s32','ModelEntry'),(262,'s32','EffectEntry'),
           (295,'u8','HeightOffset'),(296,'u8','DropColorIndex')),
    note='Name=char[32] (БЫЛ s128, перекрывал блок); SubType@80=ключ populateBooster; Price/IconPath/ModelEntry/EffectEntry HIGH; Field*=offset HIGH/семантика MED'),
 'specialitem_tooltiplist.bin': dict(stride=252, md5=16, dec=dec_special_tooltip,
    value_ledger='specialitem_tooltiplist.csv',   # ★ 2026-10-13 (wave_DATA, P65): подсказка кода 393
    tail_ledger='specialitem_tooltiplist.csv', tail_key='Id',   # ★ 2026-10-23: подсказки 79 маунтов донора AsdaFableClassic
    cols=C((0,'i32','Id'),(4,'s32','Name'),(36,'s180','Tooltip'))),
 'specialitem_buftooltiplist.bin': dict(stride=236, md5=0, dec=None,
    cols=C((0,'i32','Id'),(4,'s32','Name'),(36,'s180','Desc')),
    # ★ 2026-09-27: столбцы СМЕШАННЫЕ (640 ячеек — настоящий русский, ~33 — корейский/японский в cp949).
    #   Перекодируем только по признаку cp949, чтобы не тронуть русский.
    cp949_susp_cols=('Name', 'Desc')),
 'event_item_list.bin': dict(stride=148, md5=0, dec=None, dump_hex=True,
    cols=C((0,'i32','Idx'),(4,'s64','Str')),
    cp949_cols=('Str',),  # ★ 2026-09-27: имена обменов в cp949 (корейский/японский/русский); DataHex не трогаем
    # ★ 2026-09-25 (А1): +2 записи ЭТАЛОНА (Idx 66 «Sour Cocktail Shake Booster», 67 «Sweet Cocktail
    #   Shake Booster»): у `_MINE` архив 66 записей (9 788 б), у эталона `AsdaChroniclesInterlude` — 68
    #   (10 084 б). Записи берутся побайтно (DataHex = 148 б эталона), Idx остаётся непрерывным.
    tail_ledger='event_item_list.csv', tail_key='Idx',
    note='полная раскладка 148 MED'),
 # ★★★ 2026-09-25 (миграция оверлея): 166 строк гачи (Code 4135…4300) серверного GachaItemRateTable.txt,
 #   которых нет в клиентском архиве (1…4134), — теперь в ведомости хвоста `_tail_append/booster_list.csv`
 #   (её строит `Tools/build_itemdb_ledgers.py`), а не в `port_itemdb_union_from_server.py`. 5 клиентских
 #   предметов ссылаются на строки 4135…4138 — таблица клиента неполна (server_package_tables_sync.md).
 'booster_list.bin': dict(stride=284, md5=0, dec=None, dump_hex=True,
    value_ledger='booster_list.csv',   # ★ 2026-10-13 (wave_DATA §9): строки синхронизации 13.08 -> серверные коды (решение владельца)
    tail_ledger='booster_list.csv', tail_key='Code',
    cols=C((0,'i32','Code'))+[(4+8*k,'i32',f'Slot{k:02d}ItemCode') for k in range(35)]
                          +[(8+8*k,'i32',f'Slot{k:02d}Weight') for k in range(35)],
    note='35 пар (ItemCode + Weight=шанс/100000, Σвесов=100000) — всё HIGH'),
 # ★★★ 2026-09-27 (itemsdatafix, КРИТ-1 ревизии items_data). Гача 5 автоматов (18.08,
 #   asda_build_premium_gatya_slots.py --csv) жила ТОЛЬКО в CSV — полный декод 27.09 откатил её к архиву
 #   (356 строк вместо 583). Теперь ведомости: замены (803) + хвост (227, строки 356…582). Строит их
 #   Tools/build_itemdb_poststep_ledgers.py из эталонного CSV (восстановлен из DT, = снимку 25.09).
 'premium_gatya.bin': dict(stride=20, md5=0, dec=None,
    value_ledger='premium_gatya.csv', tail_ledger='premium_gatya.csv', tail_key='Index',
    cols=C((0,'i32','Index'),(4,'i32','SlotNumber'),(8,'i32','ItemCode'),(12,'i32','ViewSlot'),(16,'i32','Quantity')),
    note='пул наград ПРЕМИУМ-гачи; зеркало серверной ItemTable_Info\\PrimiumGaChaTable.txt. '
         'SlotNumber(+4) = серверный iSlotNumber, т.е. КАКОЙ АВТОМАТ выдаёт приз (2-я колонка txt, '
         'парсер sub_4BCAD0 кладёт её в байт rec+2, гейт Recv_UserPrimiumGaCha@0x4F6F10 требует '
         'iSlotNumber < числа строк PrimiumGaChaPriceTable.txt). ViewSlot(+12) = в ВИТРИНЕ какого '
         'автомата приз показан; эталон uiRefreshItemGridPage@0xB6EE70 фильтрует сетку 68300 именно '
         'по нему (*(rec+12) == выбранный автомат), −1 = не показывать. Quantity(+16) = 4-я колонка txt'),
 # ⚠ Запись в REGISTRY добавлена 2026-08-18: структуру FAsdaPremiumGatyaDataRow ранее дописали
 #   в АВТО-генерируемый AsdaItemDbRows.h руками, и любой прогон gen_itemdb_structs.py её стирал.
 'premium_gatya_data.bin': dict(stride=8, md5=0, dec=None,
    cols=C((0,'i32','Slot'),(4,'i32','Price')),
    note='ЦЕНА РОЗЫГРЫША премиум-гачи по слоту-автомату; зеркало серверной '
         'ItemTable_Info\\PrimiumGaChaPriceTable.txt. Читается PremiumGatya_LoadData@0xB6F720 '
         '(заголовок 20 б, счётчик @4, магия @12 "PMD", затем fread(ptr, 8, dataCount, f)), '
         'значение берёт list_GetSelectedEntryCode@0xB6F2C0 — 2-й dword записи по индексу слота, '
         'вне диапазона возвращает 0. ⚠ ДЕФЕКТ ДАННЫХ ОРИГИНАЛА: в файле ДВЕ записи ({0,690} и '
         '{1,690}), а счётчик @4 = 1, поэтому оригинал видит только первый автомат; импортируем '
         'ОБЕ (закон fix-original-defects). Столько же строк обязано быть в серверном файле — '
         'гейт Recv_UserPrimiumGaCha@0x4F6F10 требует slot < числа строк, иначе 6578 статус 0'),
 'packagelist.bin': dict(stride=36, md5=0, dec=None,
    cols=C((0,'i32','Code'),(4,'s32','AssetName'))),
 'packagetable.bin': dict(stride=56, md5=0, dec=None,
    value_ledger='packagetable.csv',   # ★ 2026-10-13 (wave_DATA §9): строки синхронизации 13.08 -> серверные коды (решение владельца)
    # ★ 2026-10-24 (fable_shop): +79 пакетов 5228…5306 «маунт 559…637 ×1» премиум-коробок 79429…79507 (сервер —
    #   те же строки в PackageItem_Table.txt). Ведомость строит `asda_fable_mounts_boxes.py`.
    tail_ledger='packagetable.csv', tail_key='Index',
    cols=C((0,'i32','Index'))+[(4+4*k,'i32',nm) for k,nm in enumerate(
        ['Item0','Qty0','Item1','Qty1','Item2','Qty2','Item3','Qty3','Item4','Qty4',
         'Item5','Qty5','Reserved52'])]),  # Item5/Qty5 = 6-я пара (HIGH: 4580,20)

 # === group B ===
 # ★ 2026-10-29 (donor_wod, Reports/donor_wod/IMPORT.md): ХВОСТ — строки экипировки/свитков/яиц донора WorldOfDreams
 #   (коды 79722…): без строки окно заточки отвергает вещь (dw04 = ранг, `IsScrollCompatible` → `GetPrice`). Порт ищет запись
 #   по КЛЮЧУ ItemCode (`BuildKeyIndex` AsdaItemTableSubsystem.cpp:663; AsdaItemResetSubsystem — перебором по ItemCode),
 #   позиция не важна. Ведомость `_tail_append/pricetable.csv` строит `AstoryClient/Tools/asda_wod_records.py` (строка аналога).
 'pricetable.bin': dict(stride=68, md5=16, dec=None,
    tail_ledger='pricetable.csv', tail_key='ItemCode',
    cols=gen_ints(13, names=['ItemCode','Price'])),
 # ★★★ 2026-09-25 (А10, решение владельца: авторитет по разборке предметов — СЕРВЕР). У 3004 из 55247
 #   общих кодов клиентский предпросмотр разборки (предмет-результат / вес / кол-во) расходится с реальной
 #   выдачей серверного `DisassembleTable.txt` (загрузчик `sub_4B0C30`, GameServer.c:70855; порядок колонок
 #   в записи 77 б: code, drawCount, затем по группам Code/Weight/MinQty/MaxQty — идентичен клиентскому).
 #   Приведение к серверу — в ведомости значений (её строит `Tools/build_itemdb_ledgers.py --values`).
 'decompositiontable.bin': dict(stride=120, md5=16, dec=None, value_ledger='decompositiontable.csv',
    cols=C((0,'i32','ItemCode'),(4,'i32','ResultDrawCount'))
        +[(8+16*g+4*f,'i32',f'Result{g}'+['Code','Weight','MinQty','MaxQty'][f]) for g in range(6) for f in range(4)],
    note='6 групп (Code,Weight,MinQty,MaxQty) — взвеш.рандом-дроп, Σвесов=100000; всё HIGH (было неверно 24 слота)'),
 'graderate.bin': dict(stride=24, md5=16, dec=None,
    cols=C((0,'i32','GradeIndex'),(4,'i32','RatePercent'))),
 'upgradetable.bin': dict(stride=44, md5=16, dec=None,
    cols=gen_ints(7, names=['EventId','LvlMin','LvlMax','Cost','RateBasePercent','ApplyItemTypeGroup','ReqTargetGrade']),
    note='RateBasePercent=базовый % успеха; ApplyItemTypeGroup=класс цели; ReqTargetGrade 5=любой — всё HIGH'),
 # ★★★ 2026-09-25 ИСТОЧНИК ПЕРЕОПРЕДЕЛЁН НА loose-ФАЙЛ `Data\NPC\RefineHigh.BIN`.
 #   БЫЛО: запись `refinehigh.bin` ИЗ `Bin.bbz` (Data\Character) — 63 804 б, 938 записей, md5
 #     a5c9fd2e71299e56ed9cf5e6c89116d1. Эту копию КЛИЕНТ НЕ ЧИТАЕТ НИГДЕ: оба загрузчика эталона
 #     открывают "./Data/NPC/RefineHigh.BIN" — `tagHighRefineShop_LoadRefineHigh`
 #     (RE/etalon/item_enhance.c:6964, «interface refine.cpp») и `CHighRefine_LoadRefineHighBin`
 #     @0xCF5FE0 (RE/etalon/ui_core.c:46433, «interface highrefine.cpp», окно 78200 Smelting3_Win).
 #     Копия в Bin.bbz — протухший срез: в ней НЕТ 56 записей (Idx 938..993) и разъехались ещё 154
 #     строки (Idx 784..937, 1140 значений); грейд 4 представлен 14 записями вместо 70.
 #   СТАЛО: loose `Data\NPC\RefineHigh.BIN` — 67 612 б, 994 записи, md5 44e9bb5cc2292fb64792a946f60ad88e.
 #     Сверка 25.09.2026 с ЖИВЫМ сервером (`GameServer\ItemTable_Info\UpperRefineTable.txt`, 994
 #     записи, загрузчик sub_4B22A0 @ GameServer.c:71410): 0 расхождений из 16 898 значений.
 #     Сопоставление по ключу Idx: наши 938 записей — строгое ПОДМНОЖЕСТВО эталонных 994,
 #     уникальных у нас НЕТ, при переходе не теряется ни одной записи.
 #   ★ Путь указывает на ЭТАЛОННУЮ установку `AsdaChroniclesInterlude` (её system\AsdaStory1.exe
 #     md5 9DBC69EB… совпадает с RE\ida\work\AsdaStory.exe, с которого снят декомпил). Копии в
 #     `_MINE`, `_MINE_WoS` и «— копия» побайтово те же (md5 44e9bb5c…) — расхождения источника нет.
 #   ★ Мирроринга в RawAssets намеренно НЕТ: у трёх других loose-таблиц (itemoptionrange,
 #     premiumleveltable, making_recipe) его тоже нет — вторая копия породила бы второй «источник
 #     истины», который молча разъедется. Протухший `RawAssets\Data\Character\refinehigh.bin`
 #     оставлен как есть (зеркало Bin.bbz), но конвейером БОЛЬШЕ НЕ ЧИТАЕТСЯ.
 #   ★ Тег в заголовке — 'ULR\x00' (не 'REF'): строка ошибки оригинала «Can't find Token REF» —
 #     copy-paste дефект самого оригинала, на него ориентироваться нельзя.
 #   ★ Сервер требует СТРОГОЙ последовательности Idx: при `предыдущий+1 != текущий` он печатает
 #     «UpperRefineTable.txt Sync Error» и БРОСАЕТ загрузку таблицы целиком. Idx 0..993 без дыр.
 'refinehigh.bin': dict(stride=68, md5=0, dec=None,
    loose=r'@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude\Data\NPC\RefineHigh.BIN',
    cols=gen_ints(17, names=['Idx','ResultRequiredLevel','RefineLevel','ResultItemType','ResultItemGrade','Reserved20',
        'Mat1Code','Mat1Qty','Mat1Price','Mat2Code','Mat2Qty','Mat2Price',
        'Mat3Code','Mat3Qty','Mat3Price','ConstField60','GoldCost']),
    note='loose Data\\NPC\\RefineHigh.BIN (994 записи, = живому серверу 1:1); '
         'ResultItemType/ResultItemGrade=ключ матча результата (HIGH); GoldCost геом.рост; ConstField60=90000'),
 'refineupmoney.bin': dict(stride=4, md5=0, dec=None, fixed=(40,4),
    cols=C((0,'i32','Money')), note='фикс.блок 40б, count(header) игнор; строка=уровень заточки'),
 'advalue.bin': dict(stride=12, md5=0, dec=None,
    cols=C((0,'i32','Index'),(4,'i32','ValA'),(8,'i32','ValB')),
    note='Index HIGH; ValA/ValB LOW (константы=3, консьюмер не локализован)'),
 'combo time.bin': dict(stride=156, md5=0, dec=None,
    cols=C((0,'i32','Field0'),(4,'i32','Field4'),(8,'s32','ClipName'),(40,'i32','StepCount'),(44,'f32','WindowTime'))
        +[(48+4*k,'f32',f'FrameTimeA{k}') for k in range(9)]
        +[(84+4*k,'f32',f'FrameTimeB{k}') for k in range(9)]
        +[(120+4*k,'f32',f'FrameTimeC{k}') for k in range(9)],
    note='ClipName=имя аним-клипа (БЫЛ порезан на ComboFlag); StepCount; 3 трека FrameTimeA/B/C ×33.333 кадры→мс'),
 'charequiptable.bin': dict(stride=16, md5=0, dec=None,
    cols=C((0,'i32','ItemCode'),(4,'i32','Gender'),(8,'i32','SlotType'),(12,'i32','PrevCode'))),
 # ★★★ 2026-09-27 (itemsdatafix, КРИТ-1): пороги опыта = серверный ExpTable.txt (asda_charexp_sync.py
 #   --apply, 11.09) — ведомость значений (233), иначе декод возвращает клиентские пороги.
 'charexp.bin': dict(stride=16, md5=0, dec=None, value_ledger='charexp.csv',
    cols=gen_ints(4, names=['ExpToNext','Zero','CumExp','Zero2'])),
 'smcharexp.bin': dict(stride=16, md5=0, dec=None,
    cols=gen_ints(4, names=['ExpToNext','Zero','CumExp','Zero2'])),
 # ★★★ 2026-09-25 (А8, решение владельца: авторитет по деньгам — СЕРВЕР). Цена причёски (+40 = `Price`)
 #   приводится к серверному `HairTypeTable.txt` (поле +69, `GameServer.c:94726` сравнивает с деньгами
 #   игрока, `:94733` списывает). Правки — в ведомости значений (198 строк), её пишет
 #   `AstoryClient/Tools/asda_hairtable_sync.py --apply`. ПЕРВОПРИЧИНА прежней потери: 28.08 цены
 #   правились только в CSV, а 31.08 этот декодер перегенерировал hairtable.csv из архива.
 'hairtable.bin': dict(stride=312, md5=0, dec=None, dump_hex=True, value_ledger='hairtable.csv',
    cols=C((0,'u16','Id'),(2,'s32','Name'),(34,'u8','Gender'),(35,'u8','RequiredLevel'),(36,'u8','Enabled'),
           (40,'i32','Price'),(44,'u8','HairStyle'),(45,'s32','Mesh'),(77,'u8','ColorIdx'),
           (78,'s32','Icon'),(110,'u8','IconCell'),(111,'s200','Description')),
    note='Gender(1М/2Ж)/Enabled/HairStyle/IconCell HIGH; хвост@111=Description (текст тултипа, не 2-й меш)'),
 'jobtype.bin': dict(stride=64, md5=0, dec=None,
    cols=C((0,'s32','JobName'),(32,'s32','ParentJob'))),

 # === group C ===
 'sowell_list.bin': dict(stride=188, md5=16, dec=dec_sowell_list, dump_hex=True,
    # ★ 2026-11-20 (админ-центр А4.4, AstoryAdminCenter/domains/cs_kinds/itemdata.py): ведомость значений — правка уровня
    #   самоцвета (RequiredLevel +65 = серверный Cellob_Table ток.16, сверено на всех 3847 общих кодах) наборами изменений панели
    #   переживает перегенерацию (правило itemdb-ledger-overlays). Файл заведён пустым (только шапка) — декод без правок прежний.
    value_ledger='sowell_list.csv',
    cols=C((0,'i32','Id'),(4,'s32','Name'),(36,'u8','BonusStatType'),(37,'u8','PartGrade'),(40,'i16','BonusValue'),
           (44,'u8','SpecialFlag'),(45,'u8','SocketTargetType'),(46,'u8','SocketTargetType2'),(48,'i32','SellPrice'),
           (52,'i32','BuyPrice'),(64,'u8','GradeReq'),(65,'u8','RequiredLevel'),(68,'i32','OptionMask'),
           (73,'s32','IconPath'),(105,'s32','MeshEntry2'),(137,'s32','MeshEntry'),(169,'u8','GradeColorIndex')),
    note='полная SowelRecord (границы доказаны пофилдовым дешифром+capstone 241 консьюмера): BonusStatType/Value, PartGrade, GradeColorIndex, GradeReq HIGH'),
 'sowell_tooltip.bin': dict(stride=220, md5=16, dec=dec_sowell_tooltip,
    # ★ 2026-09-27 (itemsfix): ведомость значений — 24 подсказки «FrankenЎЇs …» (байты cp949 ’ в строке
    #   cp1251 RU-клиента) -> «Franken’s …». Отчёт `Reports/owner_0927/items_consistency_fix.md`.
    value_ledger='sowell_tooltip.csv',
    cols=C((0,'i32','Id'),(4,'s200','Text'))),
 'sowell_calc_table.bin': dict(stride=24, md5=0, dec=None,
    cols=[(4*k,'f32',f'Coef{k}') for k in range(6)], note='коэффициенты формулы силы совела (роль каждого LOW)'),
 # ★ 2026-08-29 (порт МЕНЮ ЗВАНИЙ). Раскладка расширена и переименована ПО КОДУ окна 18700
 #   (`QuestLog_PopulateCategoryList@0xE98C00`, `ItemTooltip_RefreshStats@0xE99360`,
 #    `questItem_ValidateSelection@0xE99C30`, `uiSetSelectedTargetNameText@0xE98A50`):
 #     +4   SortNum          — колонка «№» списка, формат "%ld" (off_1420E5C)
 #     +12  ClassReq         — 0 любой · 1 class<=4 · 2 class 12..15 · 3 class 23..26
 #     +80  ColorIndex       — ГРЕЙД: цвет + нотис 1450+v (Обычный…Мифический). Имя не меняем:
 #                             на него уже завязан `UAsdaNameplateComponent::LookupTitle`.
 #     +84  Group            — вкладка фильтра: 0 Обычный · 1 Соревнование · 2 Секретные (нотисы 2405..2407)
 #     +88  Position         — 0 приставка (нотис 1466) · 1 окончание (1467)
 #     +92  CategoryLocIndex — колонка «Категория»: нотис 1460+v (Монстр/Уровень/Предмет/…)
 #     +96  Point            — очки славы за звание (сверено с сервером FameInfo.txt col42)
 #     +200 Enabled          — 0 ⇒ строка в список НЕ попадает (гейт `*(rec+200)`)
 #   Прежние имена EquipFlag/TypeLocIndex/Param96 были МИСНЕЙМАМИ (потребителей в порту не имели).
 # ★★★ 2026-09-27 (itemsdatafix, КРИТ-1): русские названия/описания и Point/Enabled/ColorIndex званий
 #   (Astory.Titles.ApplyPatch, 30.08) — ведомость значений (314 ячеек; байты DataHex вне колонок не меняются).
 'title_list.bin': dict(stride=256, md5=16, dec=dec_title_list, dump_hex=True, value_ledger='title_list.csv',
    cols=C((0,'i32','Id'),(4,'i32','SortNum'),(12,'i32','ClassReq'),(16,'s64','Name'),
           (80,'i32','ColorIndex'),(84,'i32','Group'),(88,'i32','Position'),(92,'i32','CategoryLocIndex'),
           (96,'i32','Point'),(100,'s64','Desc'))
        +[(176+4*k,'i32',f'OptionTypeId{k}') for k in range(6)]
        +[(200,'i32','Enabled')]
        +[(204+4*k,'i32',f'IsPercent{k}') for k in range(6)],
    note='OptionTypeId[6]→title_option, IsPercent[6] HIGH (значение стата даёт сервер); ColorIndex=грейд HIGH'),
 'title_list_old.bin': dict(stride=176, md5=0, dec=None, dump_hex=True,
    cols=C((0,'i32','Dw0')), note='ЛЕГАСИ: загрузчика в клиенте НЕТ, формат не реверсился -> raw'),
 'title_effect.bin': dict(stride=276, md5=16, dec=dec_title_effect,
    cols=C((0,'i32','Id'))+[(4+32*k,'s32',f'EffectName{k}') for k in range(8)]),
 'title_option.bin': dict(stride=52, md5=16, dec=dec_title_option,
    cols=C((0,'i32','Id'),(4,'s32','OptionStatName')),
    cp949_cols=('OptionStatName',),  # ★ 2026-09-27: имена статов — cp949 корейский, не cp1251
    note='словарь 109 статов (ссылка из title_list.OptionTypeId)'),
 'title_rank.bin': dict(stride=84, md5=16, dec=dec_title_rank, dump_hex=True,
    cols=C((0,'i32','Id'),(4,'i32','CategoryKey'),(8,'i32','ThresholdMin'),(12,'i32','Unk12'),(16,'i32','ThresholdMax'),
           (20,'s32','IconTexName'),(52,'i32','IconSrcX'),(56,'i32','IconSrcY'),(60,'i32','IconWidth'),(64,'i32','IconHeight')),
    note='АТЛАС ЗНАЧКОВ рейтинга: IconTexName + IconSrcX/Y/Width/Height (DrawTextureRect) HIGH; CategoryKey/Threshold HIGH'),
 'soulmateskilltable.bin': dict(stride=272, md5=0, dec=None, dump_hex=True,
    cols=C((0,'i32','Id'),(4,'s32','DisplayName'),(36,'s32','SkillName'),(68,'s200','Text'),(268,'i32','Reserved268')),
    note='SkillName@36=ресурс иконки HIGH; Reserved268=0 (не читается)'),
 # ★★★ ЕДИНСТВЕННАЯ ТАБЛИЦА РЕЕСТРА, У КОТОРОЙ ИСТОЧНИК ПЕРЕОПРЕДЕЛЁН (2026-09-02).
 #   ЧТО НЕ ТАК. В основной сборке (`CLIENT_MINE`, она же `DEFAULT_BBZ`) файл
 #   `soulsoulmatetext.bin` ВЫРОЖДЕН: 25 записей на месте, а уникальных строк всего ДВЕ —
 #   все 8 записей типа 0 несут «\nЕсли тебе кто-то понравится », все 17 записей типа 2 —
 #   «\nвы доверяете друг другу.</F>». Панель 114 `SOULINFO_WIN` («одержимость») из такой
 #   таблицы показывает один и тот же обрывок 8 и 17 раз подряд. Проверено побайтно в трёх
 #   сборках: `_MINE` 25 записей / 2 уникальные строки, `AsdaChroniclesInterlude` 25/25 (EN),
 #   `Фантазиум` 25/25 (полный литературный RU).
 #   ПОЧЕМУ ТОЧЕЧНО, А НЕ СМЕНОЙ `DEFAULT_BBZ`. Ревизия прогнала ВЕСЬ реестр (54 таблицы) по
 #   двум критериям вырожденности — испорчена РОВНО ЭТА ОДНА, остальные в `_MINE` не хуже
 #   эталонных. Менять источник глобально значило бы перетащить на весь порт чужую сборку
 #   (другие цены, другие имена, другой набор предметов) ради одной таблицы. Поэтому источник
 #   переопределяется здесь, у самой таблицы, и виден в реестре форматов.
 #   ⚠ Русский источник (`Фантазиум`) выбран потому, что порт русскоязычный (закон
 #   `always-russian`); формат записи в обеих сборках ОДИН И ТОТ ЖЕ — header 20 б, tag `SST`,
 #   25 × 204 б, поля `i32 Type` + `char[200] Text` в CP1251 без шифра, — то есть подмена
 #   источника раскладку не меняет и на других таблицах не сказывается.
 #   ★★★ ДЕФЕКТ ИСХОДНЫХ ДАННЫХ, ИСПРАВЛЯЕМ (закон `fix-original-defects`, находка ревизии
 #   `audit_2026-09-02/review_soulinfo_tooltip_frame.md` Н1). В записи 15 русского источника нет
 #   пробела между словом и открывающим тегом: `…\n\n Будучи<FC=FF006500,FB>одержимым,</F>`. Тег
 #   ширины не занимает, поэтому на экране в панели 114 читается «Будучиодержимым,».
 #   ЧТО В ОРИГИНАЛЕ: у EN-сборки `AsdaChroniclesInterlude` та же запись — `\n\n When you're
 #   <FC=FF006500,FB>'possessed',</F>`, пробел ЕСТЬ. То есть это дефект РУССКОГО перевода
 #   (Фантазиум), а не оригинала и не нашего импортёра: CSV совпадает с исходным .bin побайтно.
 'soulsoulmatetext.bin': dict(stride=204, md5=0, dec=None,
    src_bbz=r'@@ASTORY_DEV_ROOT@@\Donors\Fantazium\Data\character\Bin.bbz',
    text_fixes=[(15, 'Text',
                 '\\n\\n Будучи<FC=FF006500,FB>одержимым,</F>',
                 '\\n\\n Будучи <FC=FF006500,FB>одержимым,</F>',
                 'пробел перед тегом: без него на экране «Будучиодержимым,»; в EN-сборке пробел есть')],
    cols=C((0,'i32','Type'),(4,'s200','Text')),
    note='ИСТОЧНИК ПЕРЕОПРЕДЕЛЁН: в CLIENT_MINE файл вырожден (25 записей / 2 уникальные строки), '
         'берётся русский Фантазиум (25/25). Формат идентичен, раскладка не менялась'),
 'soulmateskill.ssi': dict(stride=112, md5=0, dec=None, noheader=True, dump_hex=True,
    cols=C((0,'u16','Id'),(2,'s40','Name'),(42,'u8','ReqLevel'),(43,'u8','ApplyBranch'),(46,'i32','CooldownMs'),
           (50,'u8','Stat1Type'),(51,'u8','Stat2Type'),
           (52,'u8','Range'),(53,'u8','CooldownType'),(76,'i16','StatBonus'),(85,'u8','PartnerCooldown'),
           (87,'u8','ClassReq'),(88,'i32','GroupId')),
    cp949_cols=('Name',),  # ★ 2026-09-27: 15 имён навыков — cp949 корейский, 30 — ASCII (русского нет); DataHex не трогаем
    note='БЕЗ header; раскладка из tooltip-билдеров+рендера иконок (capstone) HIGH. '
         'ДОБАВЛЕНО 2026-08-31 реверсом навыков Дружбы: +43 ApplyBranch (1=мгновенная ветка 5232, иначе 5233; '
         'в данных 0 у всех 45), +46 = ПОЛНЫЙ КУЛДАУН мс (был назван Cost; social.c:5872, network.c:28872), '
         '+85 PartnerCooldown (1=кулдаун стартует и у партнёра; network.c:45518, GameServer.c:63258), '
         '+88 GroupId = ГРУППА СТУПЕНЕЙ навыка (1={30,36,41} 2={34,38,43} 3={33,37,42}, -1=одиночный)'),
 'petexptable.bin': dict(stride=44, md5=0, dec=None,
    cols=C((0,'i32','Level'),(4,'i32','Exp'),(8,'i32','CostBase'),(12,'s32','StageName')),
    note='StageName="Larva(1)".. (БЫЛ порезан на int); CostBase=база дюр/наград/fail% — всё HIGH'),
 'petoptiontable.bin': dict(stride=68, md5=0, dec=None,
    cols=C((0,'i32','Id'),(4,'s64','Format')),
    note='Format=char[64] printf-шаблон опции (БЫЛ порезан на Unk36..64) — оба HIGH'),
 'pettable.bin': dict(stride=472, md5=0, dec=None, dump_hex=True,
    cols=C((0,'i32','Id'),(4,'s64','Name'),(68,'i32','Grade'),(72,'i32','Unk72'),(76,'i32','Category'),
           (80,'i32','DurabilityFactor'),(84,'i32','FeedCostFactor'),(88,'i32','GoldRewardFactor'),(92,'i32','ExpRewardFactor'),
           (96,'i32','IsCombatPet'),(100,'i32','StatMod'),(104,'i32','EfficiencyPct'),(108,'i32','OptionId0'),(112,'i32','OptionValue0'),
           (116,'i32','OptionId1'),(120,'i32','OptionValue1'),(132,'i32','OptRarityCode0'),(136,'i32','OptRarityCode1'),
           (140,'i32','OptRarityCode2'),(144,'i32','Unk144'),(148,'s32','ModelBase'),(180,'s32','ModelPart0'),(212,'s32','ModelPart1'),
           (244,'s32','ModelPart2'),(280,'s32','ResultIcon0'),(312,'i32','ResultIcon0U'),(316,'i32','ResultIcon0V'),
           (320,'s32','ResultIcon1'),(360,'s32','IconTexture'),(392,'i32','IconU'),(396,'i32','IconV'),(468,'i32','WeaponClass')),
    note='Grade/факторы наград/OptionId1→petoption/модель+части/иконка HIGH; боевые статы серверные'),
 'skilltooltip.bin': dict(stride=504, md5=0, dec=None, dump_hex=True,
    cols=C((0,'i32','Id'),(4,'s32','Name'),(100,'s404','Text'))),
 'emotiontip.bin': dict(stride=265, md5=0, dec=None, dump_hex=True,
    cols=C((0,'s32','Name'),(32,'s32','Tag'),(64,'u8','Type'))),
 'loginhelpertext.bin': dict(stride=204, md5=0, dec=None,
    cols=C((0,'i32','TabIndex'),(4,'s200','Text')), note='TabIndex=UI-таб 0/1/2 (ui3Tab_SetActiveByMode) HIGH'),
 'stateinfotip.bin': dict(stride=272, md5=0, dec=None, dump_hex=True,
    cols=C((0,'i32','Id'),(4,'s32','Name'),(36,'s32','Icon'),(68,'i32','IconAtlasIndex'),(72,'s200','Text')),
    note='IconAtlasIndex@68 (IconAtlas_IndexToUV) HIGH — разрыв Icon..Text заполнен'),
 'soulstateinfotip.bin': dict(stride=272, md5=0, dec=None, dump_hex=True,
    cols=C((0,'i32','Id'),(4,'s32','Name'),(36,'s32','Icon'),(68,'i32','IconAtlasIndex'),(72,'s200','Text')),
    note='IconAtlasIndex@68 (IconAtlas_IndexToUV) HIGH — разрыв Icon..Text заполнен'),
 'skill_info.sif': dict(stride=273, md5=0, dec=None, noheader=True, dump_hex=True,
    cols=C((0,'u16','SkillCode'),(2,'s40','Name'),(42,'u8','BehaviorType')),
    # ★ 2026-09-25: ведомость приведения числовых полей навыка к серверу (`SkillInfo.sif`).
    #   Поля лежат в DataHex (именованных колонок нет), поэтому адресуются через `ledger_fields`
    #   {имя: (offset, тип)}. Смещения/типы взяты из ЕДИНСТВЕННОГО рантайм-консьюмера порта
    #   `AsdaSkillDatabase.cpp` (DataHex -> AsdaBytes::Safe*): cooldownMs=SafeI32@0x2F, msBase=SafeI16@0x3E,
    #   msPerLevel=SafeI16@0x40, buff0SubType=SafeU8@0x47, s1Base=SafeI16@0x4A, buff0Duration=SafeI32@0x4E,
    #   s2Base=SafeI16@0x5B, buff1Duration=SafeI32@0x5F, weaponMask@0x83 (8-байтная маска; правятся
    #   младшие 2 байта, старшие у затронутых навыков нулевые). Правка ложится на байты записи -> DataHex,
    #   поэтому переживает и перегенерацию декодером, и переимпорт DT_skill_info. Все 273 б, кроме адресуемых,
    #   в т.ч. 64-б хвост подсказки (+209…+272), остаются 1:1 с архивом.
    value_ledger='skill_info.sif.csv',
    # ★ 20.11.2026 (админ-центр А4.7 «Навыки и классы», AstoryAdminCenter/domains/cs_kinds/skilldata.py): ещё поля записи,
    #   которые правит панель парой с серверным SkillInfo.sif (байты +41…+208 клиента = серверной записи у всех 454 навыков игроков,
    #   сверка 20.11) — сервер их читает (sub_4760E0 GameServer.c:47788…47819, NpcServer.exe.c:12578…12587), клиент — те же смещения
    #   (AsdaSkillDatabase.cpp). Прежние имена и строки ведомости не меняются.
    ledger_fields=dict(cooldownMs=(47, 'i32'), msBase=(62, 'i16'), msPerLevel=(64, 'i16'),
                       buff0SubType=(71, 'u8'), s1Base=(74, 'i16'), buff0Duration=(78, 'i32'),
                       s2Base=(91, 'i16'), buff1Duration=(95, 'i32'), weaponMask=(131, 'u16'),
                       maxLevel=(43, 'u8'), rangeMeters=(53, 'u8'), aoeRadius=(55, 'u8'), mpBase=(58, 'i16'),
                       mpPerLevel=(60, 'i16'), skillPerBase=(66, 'u8'), skillPerPerLevel=(67, 'u8'), srBase=(68, 'u8'),
                       srPerLevel=(69, 'u8'), s1PerLevel=(76, 'i16'), buff0DurPerLevel=(82, 'i32'), s2PerLevel=(93, 'i16'),
                       buff1DurPerLevel=(99, 'i32'), s3Base=(108, 'i16'), s3PerLevel=(110, 'i16'), buff2Duration=(112, 'i32'),
                       buff2DurPerLevel=(116, 'i32'), needPoints=(190, 'u8')),
    note='SkillRecord: SkillCode/Name/BehaviorType HIGH; ~40 числ.полей в мастер-IDB (не текст-эталон) — в DataHex'),
}

# записи ZIP, которые НЕ таблицы (мусор/нереверсированные) — пропускаем c пометкой
SKIP = {'mode.zip','m.zip','mod.zip','nuevo documento de texto.txt','females.bat','original.bat',
        'skillinfo.skill','skill_info.org'}

# ---------------------------------------------------------------------------
def read_header(data):
    ver   = struct.unpack_from('<f', data, 0)[0]
    count = struct.unpack_from('<i', data, 4)[0]
    extra = struct.unpack_from('<i', data, 8)[0]
    tag   = bytes(data[12:16])
    return ver, count, extra, tag

def _typed(typ, text):
    """Значение из ведомости (строка CSV) -> тип колонки реестра (как его вернул бы rd())."""
    if typ.startswith('s'):
        return text
    if typ == 'f32':
        return round(float(text), 6)
    return int(text)


def load_value_ledger(name, spec, cols):
    """Ведомость значений `value_ledger` -> список правок формы text_fixes (строка, колонка, было, стало, почему).

    Правки идут в тот же apply_fixes(), что и дефекты оригинала: накладываются НА БАЙТЫ записи до разбора
    колонок (колонка и DataHex согласованы) и ТОЛЬКО если в данных лежит «было» — иначе предупреждение."""
    fn = spec.get('value_ledger')
    if not fn:
        return []
    path = os.path.join(VALUE_LEDGER_DIR, fn)
    if not os.path.isfile(path):
        print(f'    ! {name}: нет ведомости значений {path} — правки НЕ наложены')
        return []
    typ_by_name = {nm: typ for (off, typ, nm) in cols}
    # ★ 2026-09-25: поля ведомости, которых НЕТ среди именованных колонок CSV (лежат целиком в DataHex),
    #   объявляются в реестре как `ledger_fields` = {имя: (offset, тип)}. Так ведомость значений может
    #   адресовать числовые поля записи (напр. навыки skill_info.sif: cooldownMs@47, msBase@62 …),
    #   не раздувая схему DataTable новыми колонками. Патч ложится на байты записи -> отражается в DataHex.
    typ_by_name.update({nm: typ for nm, (off, typ) in spec.get('ledger_fields', {}).items()})
    out = []
    # ★ дедуп №2 В2 Т8: чтение — общий `asda_ledger.value_overlay_entries` (тот же csv.DictReader, utf-8, все колонки).
    for r in _LG.value_overlay_entries(path):
        col = r['column']
        if col not in typ_by_name:
            print(f'    ! {name}: ведомость значений: нет колонки {col!r} — строка пропущена')
            continue
        typ = typ_by_name[col]
        out.append((int(r['row']), col, _typed(typ, r['was']), _typed(typ, r['now']),
                    f"ведомость {fn}: {r.get('source', '')} {r.get('reason', '')}".strip()))
    return out


def append_tail(name, spec, rows, cols, rec_len, dump_hex):
    """Дописать записи ведомости `tail_ledger` В ХВОСТ rows. Возвращает число дописанных (0 — отказ/нет).

    СТРОГО и АТОМАРНО (либо все строки ведомости, либо ни одной):
      • `_row` ведомости обязан продолжать таблицу без дыр (len(rows), len(rows)+1, …) — у позиционных
        таблиц иначе съехали бы все ссылки; если источник вырос/сменился — отказ, пересобрать ведомость;
      • ключ `tail_key` дописываемой записи не должен уже встречаться в таблице (и в самой ведомости);
      • у таблиц с dump_hex запись берётся ПОБАЙТНО из DataHex ведомости (длина = записи без MD5-префикса),
        колонки разбираются из этих байтов тем же rd() и обязаны совпасть с колонками ведомости;
      • у прочих колонки берутся из ведомости с приведением к типу реестра."""
    fn = spec.get('tail_ledger')
    if not fn:
        return 0
    path = os.path.join(TAIL_LEDGER_DIR, fn)
    if not os.path.isfile(path):
        print(f'    ! {name}: нет ведомости хвоста {path} — хвост НЕ дописан')
        return 0
    csv.field_size_limit(1 << 30)
    with open(path, 'r', encoding='utf-8', newline='') as f:
        led = list(csv.DictReader(f))
    key = spec.get('tail_key')
    have = {r.get(key) for r in rows} if key else set()
    new_rows, errors = [], []
    for j, r in enumerate(led):
        want = len(rows) + j
        if int(r['_row']) != want:
            errors.append(f'_row {r["_row"]} вместо {want} (дыра/сдвиг — источник изменился?)')
            break
        row = {'_row': want}
        if dump_hex:
            b = bytes.fromhex(r['DataHex'])
            if len(b) != rec_len:
                errors.append(f'_row {want}: DataHex {len(b)} б вместо {rec_len}')
                break
            for (off, typ, nm) in cols:
                row[nm] = rd(b, off, typ)
                if str(row[nm]) != r[nm]:
                    errors.append(f'_row {want}.{nm}: из байтов {row[nm]!r}, в ведомости {r[nm]!r}')
            row['DataHex'] = b.hex()
        else:
            for (off, typ, nm) in cols:
                row[nm] = _typed(typ, r[nm])
        if key:
            if row[key] in have:
                errors.append(f'_row {want}: ключ {key}={row[key]} уже есть в таблице')
            have.add(row[key])
        new_rows.append(row)
    if errors:
        for e in errors[:10]:
            print(f'    ! {name}: ведомость хвоста {fn}: {e}')
        print(f'    ! {name}: ХВОСТ НЕ ДОПИСАН (ошибок {len(errors)}) — таблица осталась 1:1 с архивом')
        return 0
    rows.extend(new_rows)
    print(f'    + {name}: дописано в хвост {len(new_rows)} записей из ведомости {fn}'
          f' (строки {len(rows) - len(new_rows)}…{len(rows) - 1})')
    return len(new_rows)


def apply_dedup_map(name, spec, rows, cols, dmap):
    """★★★ 2026-11-01 (item_dedup, этап 2; решение владельца 01.11.2026). ПЕРЕКОДИРОВКА ССЫЛОК НА ПРЕДМЕТЫ-ДУБЛИ.

    Карта `AstoryClient/Tools/data/item_dedup_map.json` («дубль → основной», все три пространства кодов) — через
    общий модуль `asda_item_dedup`. Меняются ТОЛЬКО колонки-ссылки на чужие предметы (`_DD.ITEMDB_CODE_FIELDS`:
    барабаны бустеров, состав коробок, пул премиум-гачи, обмены у NPC +68/+108, стартовая экипировка, разборка, крафт,
    высокая ковка); строки самих дублей (item_list и др., ключ = код) не трогаются — плотный массив «индекс = код − база».
    Шаг идёт ПОСЛЕ ведомостей (_value_overlay и _tail_append): их «было» сверяется с архивом, а итог перекодируется здесь.
    У таблиц с DataHex код в колонке и его копия в DataHex меняются вместе. Возврат — число заменённых значений."""
    base = os.path.splitext(name)[0].replace(' ', '_')
    fields = _DD.itemdb_code_fields(base)
    if not fields or not dmap:
        return 0
    # Сторож раскладки: смещения общего модуля обязаны совпадать с реестром этого декодера.
    by_name = {nm: (off, typ) for (off, typ, nm) in cols}
    dump_hex = spec.get('dump_hex', False)
    for col, off in fields:
        if col is None:
            if not dump_hex:
                raise RuntimeError(f'{name}: поле-код DataHex+{off} у таблицы без DataHex — раскладка разошлась')
        elif by_name.get(col) != (off, 'i32'):
            raise RuntimeError(f'{name}: колонка-код {col} в asda_item_dedup = +{off} i32, в реестре {by_name.get(col)}')
    if (base in _DD.ITEMDB_DUMP_HEX) != bool(dump_hex):
        raise RuntimeError(f'{name}: признак DataHex в asda_item_dedup и в реестре разошёлся')
    n = 0
    for r in rows:
        n += len(_DD.remap_itemdb_row(base, r, dmap))
    if n:
        print(f'    ~ {name}: перекодировано ссылок на предметы-дубли: {n} (item_dedup_map.json)')
    return n


def decode_table(name, data, spec, use_ledgers=True, dedup_map=None):
    stride = spec['stride']; md5 = spec.get('md5', 0)
    dec = spec.get('dec'); cols = spec['cols']; dump_hex = spec.get('dump_hex', False)
    noheader = spec.get('noheader', False); fixed = spec.get('fixed')

    result = {'entry': name, 'usize': len(data)}
    if noheader:
        hsize = 0; hcount = None; tag = b''; ver = None
        body = data
    else:
        hsize = 20
        ver, hcount, extra, tag = read_header(data)
        body = data[20:]

    # ---- определить число записей + валидация count ----
    if fixed:
        fbytes, unit = fixed
        n_expected = fbytes // unit          # refineupmoney: 40/4 = 10
        n = n_expected
        count_ok = True
        result['count_header'] = hcount
        result['count_used'] = n
        result['count_ok'] = (len(body) >= fbytes)
        result['note_count'] = f'фикс.блок {fbytes}б (header count={hcount} игнор загрузчиком)'
        body = body[:fbytes]
    else:
        avail = len(body)
        n_calc = avail // stride
        rem = avail % stride
        if noheader:
            n = n_calc
            result['count_header'] = None
            result['count_used'] = n
            result['count_ok'] = (rem == 0)
        else:
            n = hcount
            result['count_header'] = hcount
            result['count_used'] = n
            result['count_ok'] = (rem == 0 and n_calc == hcount)
            result['count_calc'] = n_calc
        if rem != 0:
            result['note_count'] = f'ОСТАТОК {rem}б при stride={stride} (n_calc={n_calc})'

    result['tag'] = tag.decode('latin-1', 'replace').rstrip('\x00')
    if not noheader:
        result['ver'] = round(ver, 4)

    # ---- ★★★ ТОЧЕЧНЫЕ ИСПРАВЛЕНИЯ ДЕФЕКТОВ ИСХОДНЫХ ДАННЫХ (закон `fix-original-defects`) ----
    #   ЗАЧЕМ ЗДЕСЬ, А НЕ В CSV. Правка руками в `RawAssets/.../*.csv` живёт до первой перегенерации:
    #   этот скрипт перезаписывает CSV/JSON целиком (тот же класс граблей, что у генерируемого
    #   заголовка ItemDB). Поэтому исправление лежит РЯДОМ С РАСКЛАДКОЙ (в реестре таблицы —
    #   `text_fixes`, либо по коду предмета — `ORIGINAL_DEFECT_FIXES`) и применяется на каждом декоде.
    #   ФОРМА: text_fixes=[(номер записи, имя колонки, 'было', 'стало', 'почему')].
    #   ⚠ СТРОГО: если в данных лежит НЕ 'было' — правка НЕ применяется, а пишется предупреждение.
    #   Молча подменять текст нельзя: источник может смениться, и тогда правка обязана быть
    #   пересмотрена человеком, а не наложена вслепую.
    #   ★ 2026-09-06: правка накладывается НА БАЙТЫ записи (после снятия MD5/шифра) ДО разбора
    #   колонок — так колонка и `DataHex` согласованы, а DataTable получает ту же запись, что CSV.
    #   Раньше подменялось уже разобранное значение, и у таблиц с dump_hex сырой остаток хранил старое.
    fixes_by_row = {}
    # ★ 2026-09-25: к правкам дефектов добавлена ведомость значений (value_ledger) — тот же механизм
    #   «было → стало на байтах записи»; при --no-ledgers она не читается (декод 1:1 с архивом).
    value_fixes = load_value_ledger(name, spec, cols) if use_ledgers else []
    for fx in list(spec.get('text_fixes', ())) + expand_defect_fixes(name) + value_fixes:
        fixes_by_row.setdefault(fx[0], []).append(fx)
    col_by_name = {nm: (off, typ) for (off, typ, nm) in cols}
    # ★ 2026-09-25: разрешаем ведомости адресовать поля из `ledger_fields` (offset/тип задан в реестре,
    #   в CSV-колонках их нет) — apply_fixes() найдёт их по имени и пропатчит те же байты записи.
    col_by_name.update(spec.get('ledger_fields', {}))
    fixes_applied = 0

    def apply_fixes(i, b):
        nonlocal fixes_applied
        for (fi, fcol, fold, fnew, fwhy) in fixes_by_row.get(i, ()):
            if fcol not in col_by_name:
                print(f'    ! {name}: правка записи {fi}.{fcol} НЕ применена — нет такой колонки')
                continue
            off, typ = col_by_name[fcol]
            cur = rd(b, off, typ)
            # ★ ИСПРАВЛЕНИЕ дедуп №2 В2 Т8: решение — общее `asda_ledger.classify`. В данных УЖЕ «стало» (источник сам
            #   получил это значение) — правка считается наложенной (раньше — предупреждение «другое значение» и статус
            #   CHECK, хотя итог верный). «Было» — накладываем; иначе — прежнее предупреждение.
            cls = _LG.classify(cur, fnew, fold)
            if cls == _LG.ALREADY:
                fixes_applied += 1
                print(f'    = {name}: запись {fi}.{fcol} уже {fnew!r} — правка не нужна ({fwhy})')
                continue
            if cls != _LG.APPLY:
                print(f'    ! {name}: правка записи {fi}.{fcol} НЕ применена — в данных другое значение '
                      f'({cur!r} вместо {fold!r}); проверить источник')
                continue
            if typ.startswith('s'):
                width = int(typ[1:])
                enc = fnew.encode('cp1251')
                if len(enc) >= width:
                    print(f'    ! {name}: правка записи {fi}.{fcol} НЕ применена — {len(enc)} б не входит в s{width}')
                    continue
                b[off:off+width] = enc + b'\x00' * (width - len(enc))
            else:
                fmt = {'i32':'<i','u32':'<I','i16':'<h','u16':'<H','i8':'<b','u8':'<B','f32':'<f'}[typ]
                struct.pack_into(fmt, b, off, fnew)
            if rd(b, off, typ) != fnew:
                print(f'    ! {name}: правка записи {fi}.{fcol} НЕ прошла обратную проверку')
                continue
            fixes_applied += 1
            print(f'    * {name}: запись {fi}.{fcol} исправлена ({fwhy})')

    # ---- декод записей ----
    rows = []
    for i in range(n):
        rec = body[i*stride:(i+1)*stride]
        if len(rec) < stride:
            result['truncated_at'] = i
            break
        b = bytearray(rec[md5:])           # снять MD5-префикс
        if dec:
            dec(b, i)
        if i in fixes_by_row:
            apply_fixes(i, b)
        row = {'_row': i}
        for (off, typ, nm) in cols:
            try:
                row[nm] = rd(b, off, typ)
            except Exception as e:
                row[nm] = f'<err:{e}>'
        # ★ 2026-09-27: поля, чьи байты на деле cp949 (см. fix_cp949_field). `cp949_cols` — сплошная
        #   перекодировка (в столбце нет настоящего cp1251), `cp949_susp_cols` — только по признаку
        #   (смешанный столбец: настоящий русский рядом с корейским).
        for nm in spec.get('cp949_cols', ()):
            if nm in row and isinstance(row[nm], str):
                row[nm] = fix_cp949_field(row[nm], True)
        for nm in spec.get('cp949_susp_cols', ()):
            if nm in row and isinstance(row[nm], str):
                row[nm] = fix_cp949_field(row[nm], False)
        if dump_hex:
            row['DataHex'] = bytes(b).hex()   # имя совпадает с UPROPERTY DataHex (иначе редакторный импорт роняет строки)
        rows.append(row)

    expected = sum(len(v) for v in fixes_by_row.values())
    for fi in fixes_by_row:
        if fi >= len(rows):
            print(f'    ! {name}: правки записи {fi} не применены — записи нет (всего {len(rows)})')
    if expected:
        print(f'    = {name}: применено {fixes_applied} правок из {expected} (дефекты оригинала + ведомость значений)')
        result['defect_fixes_applied'] = fixes_applied
        result['defect_fixes_expected'] = expected

    # ★ 2026-09-25: дозапись хвоста из ведомости (tail_ledger) — ПОСЛЕ всех записей архива.
    if use_ledgers and spec.get('tail_ledger'):
        result['tail_appended'] = append_tail(name, spec, rows, cols, stride - md5, dump_hex)

    # ★ 2026-11-01 (item_dedup): перекодировка ссылок на предметы-дубли — ПОСЛЕ ведомостей (значений и хвоста).
    #   dedup_map=None/{} (ключ --no-dedup-map) — шаг выключен, таблица как архив + ведомости.
    if dedup_map:
        result['dedup_replaced'] = apply_dedup_map(name, spec, rows, cols, dedup_map)

    result['rows'] = rows
    return result

# ---------------------------------------------------------------------------
def write_outputs(res, outdir):
    base = os.path.splitext(res['entry'])[0].replace(' ', '_')
    rows = res['rows']
    # порядок колонок: _row + объявленные + _data_hex
    if rows:
        keys = list(rows[0].keys())
    else:
        keys = ['_row']
    csvp = os.path.join(outdir, base + '.csv')
    with open(csvp, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction='ignore')
        w.writeheader()
        for r in rows:
            w.writerow(r)
    # ★ В JSON служебная колонка называется `Name`, а не `_row`. Причина: у CSV-импортёра UE
    #   имя строки берётся из ПЕРВОЙ колонки (её заголовок не важен), а у JSON-импортёра — из поля
    #   `Name`; любое ДРУГОЕ поле, которого нет в USTRUCT, он ругает
    #   «Property '_row' on row 'N' cannot be found in struct '…'». Эти файлы лежат внутри Content,
    #   поэтому редактор их авто-импортирует и такие сообщения попадают владельцу в лог.
    jrows = []
    for r in rows:
        d = {'Name': str(r['_row'])}
        d.update({k: v for k, v in r.items() if k != '_row'})
        jrows.append(d)
    jsonp = os.path.join(outdir, base + '.json')
    with open(jsonp, 'w', encoding='utf-8') as f:
        json.dump(jrows, f, ensure_ascii=False, indent=0)
    return csvp

# ---------------------------------------------------------------------------
def _csv_cells_diff(a_path, b_path):
    """Сравнить два CSV по `_row` и ВСЕМ колонкам. -> (строк_a, строк_b, {колонка: ячеек}, только_a, только_b, заголовок_равен)."""
    csv.field_size_limit(1 << 30)
    def load(p):
        with open(p, 'r', encoding='utf-8-sig', newline='') as f:
            rows = list(csv.DictReader(f))
        return rows, (list(rows[0].keys()) if rows else [])
    ra, ha = load(a_path)
    rb, hb = load(b_path)
    ka = {r.get('_row'): r for r in ra}
    kb = {r.get('_row'): r for r in rb}
    per = {}
    for k, r in ka.items():
        o = kb.get(k)
        if o is None:
            continue
        for c in ha:
            if r.get(c) != o.get(c):
                per[c] = per.get(c, 0) + 1
    only_a = sum(1 for k in ka if k not in kb)
    only_b = sum(1 for k in kb if k not in ka)
    return len(ra), len(rb), per, only_a, only_b, ha == hb


def _promote_stage(summary, stage, live_dir, blocked, accept_diff):
    """★ 2026-09-27 (itemsdatafix). Перенос таблиц из промежуточного каталога в БОЕВОЙ.
    Совпадает с боевым по всем колонкам -> копируется (данные те же); отличается -> только с accept_diff
    и после бэкапа боевых CSV/JSON в _backup/<дата>_decode_itemdb/<время>/. CHECK (ведомость не легла)
    и таблицы с неналоженным слоем имён — не копируются никогда. Возвращает 0 или 1 (есть отказы)."""
    import datetime as _dt, shutil as _sh
    bak = os.path.join(r'@@ASTORY_DEV_ROOT@@\_backup', _dt.date.today().strftime('%Y-%m-%d') + '_decode_itemdb',
                       os.path.basename(stage))
    refused = 0
    print('\n=== ПЕРЕНОС В БОЕВОЙ КАТАЛОГ ===')
    for key, status, *_ in summary:
        if status not in ('OK', 'CHECK'):
            continue
        base = os.path.splitext(key)[0].replace(' ', '_')
        src_csv = os.path.join(stage, base + '.csv')
        if not os.path.isfile(src_csv):
            continue
        if status == 'CHECK':
            print(f'  ! {base}: статус CHECK (ведомость/счётчик) — НЕ копирую, разобрать вручную')
            refused += 1
            continue
        if key in blocked:
            print(f'  ! {base}: слой имён не наложен — НЕ копирую')
            refused += 1
            continue
        dst_csv = os.path.join(live_dir, base + '.csv')
        if os.path.isfile(dst_csv):
            na, nb, per, oa, ob, heq = _csv_cells_diff(dst_csv, src_csv)
            same = heq and na == nb and not per and not oa and not ob
            if not same:
                print(f'  ≠ {base}: боевой {na} строк, декод {nb}; заголовок равен={heq}; только в боевом {oa},'
                      f' только в декоде {ob}; ячеек по колонкам {per}')
                if not accept_diff:
                    print(f'  ! {base}: отличается от боевого — НЕ копирую. Если разница ожидаемая (правка ведомости),'
                          f' повтори с --accept-diff; если нет — эту правку надо сперва перенести в ведомость'
                          f' (Tools/build_itemdb_poststep_ledgers.py / build_itemdb_ledgers.py).')
                    refused += 1
                    continue
            os.makedirs(bak, exist_ok=True)
            bak_ok = True
            for ext in ('.csv', '.json'):
                p = os.path.join(live_dir, base + ext)
                if os.path.isfile(p):
                    _sh.copy2(p, os.path.join(bak, base + ext))
                    if os.path.getsize(p) != os.path.getsize(os.path.join(bak, base + ext)):
                        bak_ok = False
            if not bak_ok:
                print(f'  ! {base}: бэкап не совпал по размеру — НЕ копирую')
                refused += 1
                continue
            print(f'  = {base}: {"совпадает с боевым" if same else "ОТЛИЧАЕТСЯ, --accept-diff"}; бэкап -> {bak}')
        for ext in ('.csv', '.json'):
            s = os.path.join(stage, base + ext)
            if os.path.isfile(s):
                _sh.copy2(s, os.path.join(live_dir, base + ext))
        print(f'  + {base}: скопирован в {live_dir}')
    return 1 if refused else 0


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bbz', nargs='?', default=DEFAULT_BBZ)
    ap.add_argument('--out', default=DEFAULT_OUT)
    # ★ 2026-08-29: точечная перегенерация ОДНОЙ таблицы. Без этого правка раскладки одной
    #   таблицы заставляла переписать все 52 CSV (в т.ч. item_list, которому потом нужен
    #   отдельный проход восстановления имён — см. шапку файла).
    ap.add_argument('--only', default='',
                    help='через запятую: имена записей архива (напр. title_list.bin). Пусто = все.')
    # ★ 2026-09-25: чистый декод 1:1 с архивом БЕЗ ведомостей объединения (хвосты + замены значений).
    #   Нужен аудиту/сверке с эталоном; в боевой каталог порта так выгружать НЕЛЬЗЯ — потеряется обогащение.
    ap.add_argument('--no-ledgers', action='store_true',
                    help='не накладывать ведомости _tail_append/_value_overlay (чистый декод архива)')
    # ★ 2026-09-27 (itemsdatafix): осознанное согласие заменить боевой CSV, который ОТЛИЧАЕТСЯ от декода.
    ap.add_argument('--accept-diff', action='store_true',
                    help='боевой каталог: копировать таблицы, чей CSV отличается от боевого (с бэкапом)')
    # ★ 2026-11-01 (item_dedup, этап 2): шаг перекодировки ссылок на предметы-дубли по карте item_dedup_map.json
    #   (после ведомостей). Выключить — для аудита/сверки с архивом; в боевой каталог без шага — ОТКАЗ (вернутся дубли).
    ap.add_argument('--no-dedup-map', action='store_true',
                    help='не перекодировать ссылки на предметы-дубли (карта AstoryClient/Tools/data/item_dedup_map.json)')
    args = ap.parse_args()
    # ★ 2026-09-27 (itemsfix, МЕЛК-8 ревизии): консоль cp1251 роняла скрипт на печати «−» (U+2212) из текста
    #   ведомости — до записи, но это толкало к обходам. Незакодируемые знаки теперь печатаются как «?».
    for _st in (sys.stdout, sys.stderr):
        try:
            _st.reconfigure(errors='replace')
        except Exception:
            pass
    only = {s.strip().lower() for s in args.only.split(',') if s.strip()}
    use_ledgers = not args.no_ledgers

    # ★★★ 2026-09-27 (itemsdatafix, КРИТ-1 ревизии items_data). ЗАЩИТА БОЕВОГО КАТАЛОГА.
    #   27.09 полный декод дважды выгрузили прямо в RawAssets/Data/ItemDB — CSV трёх таблиц (гача, звания,
    #   опыт) откатились к архиву. Теперь:
    #     • полный декод (без --only) в боевой каталог — ОТКАЗ: только в отдельную папку --out DIR;
    #     • --no-ledgers в боевой каталог — ОТКАЗ;
    #     • с --only таблицы сначала декодируются в ПРОМЕЖУТОЧНЫЙ каталог (Tools/_out/decode_stage/<время>),
    #       туда же накладывается слой имён; затем каждый CSV сравнивается с боевым ПО ВСЕМ колонкам.
    #       Совпал — боевой не трогается. Отличается — печатается дифф и таблица НЕ копируется, пока не
    #       дан --accept-diff (тогда боевые CSV/JSON сперва копируются в _backup/<дата>_decode_itemdb/).
    #   Так правка, которая жила только в CSV (не в ведомости), больше не пропадает молча.
    live = os.path.normcase(os.path.abspath(args.out)) == os.path.normcase(os.path.abspath(DEFAULT_OUT))
    stage = None
    if live:
        if not only:
            print('!!! ОТКАЗ: полный декод в БОЕВОЙ каталог запрещён (КРИТ-1 27.09). Выгружай в отдельную папку'
                  ' --out DIR, сравнивай, а в боевой — только нужные таблицы через --only <имя.bin>.')
            sys.exit(2)
        if not use_ledgers:
            print('!!! ОТКАЗ: --no-ledgers в БОЕВОЙ каталог запрещён — потеряется обогащение из ведомостей.')
            sys.exit(2)
        if args.no_dedup_map:
            print('!!! ОТКАЗ: --no-dedup-map в БОЕВОЙ каталог запрещён — вернутся ссылки на предметы-дубли (item_dedup).')
            sys.exit(2)
        import datetime as _dt
        stage = os.path.join(os.path.dirname(os.path.abspath(__file__)), '_out', 'decode_stage',
                             _dt.datetime.now().strftime('%Y%m%d_%H%M%S'))
        os.makedirs(stage, exist_ok=True)
        print(f'[stage] боевой каталог: таблицы сначала пишутся в {stage}')
    write_dir = stage if live else args.out

    os.makedirs(args.out, exist_ok=True)
    z = zipfile.ZipFile(args.bbz)
    entries = {n.lower(): n for n in z.namelist()}
    # Кэш архивов-источников для таблиц с `src_bbz` (открывать ZIP по разу, а не на каждую запись).
    alt_zips, alt_entries = {}, {}

    summary = []
    print(f'[bbz] {args.bbz}  entries={len(entries)}  out={args.out}')
    # ★ 2026-11-01 (item_dedup): карта читается при КАЖДОМ запуске (её перегенерируют — добавятся коды C4).
    dedup_map = {} if args.no_dedup_map else _DD.load_map()
    dedup_total = 0
    print(f'[dedup] ' + (f'карта {_DD.MAP_PATH}: {len(dedup_map)} кодов-дублей' if dedup_map
                         else 'перекодировка ссылок на предметы-дубли ВЫКЛЮЧЕНА (--no-dedup-map)'))
    for key, spec in REGISTRY.items():
        if only and key.lower() not in only:
            continue
        loose = spec.get('loose')          # loose-файл на диске (НЕ в Bin.bbz)
        if loose:
            if not os.path.isfile(loose):
                summary.append((key, 'MISSING', None, None, 'нет loose-файла'))
                print(f'  ! {key:34s} MISSING (loose)')
                continue
            with open(loose, 'rb') as lf:
                data = lf.read()
        else:
            # ★ ТОЧЕЧНОЕ ПЕРЕОПРЕДЕЛЕНИЕ ИСТОЧНИКА (`src_bbz` у записи реестра). Нужно там, где
            #   файл в основной сборке ВЫРОЖДЕН, а формат в годной сборке тот же (единственный
            #   случай на сегодня — `soulsoulmatetext.bin`, обоснование в самой записи реестра).
            #   Общий `--bbz` при этом НЕ меняется: остальные 53 таблицы читаются как читались.
            src_bbz = spec.get('src_bbz')
            zsrc, esrc, where = z, entries, args.bbz
            if src_bbz:
                if not os.path.isfile(src_bbz):
                    summary.append((key, 'MISSING', None, None, f'нет источника-переопределения {src_bbz}'))
                    print(f'  ! {key:34s} MISSING (src_bbz {src_bbz})')
                    continue
                zsrc = alt_zips.get(src_bbz)
                if zsrc is None:
                    zsrc = zipfile.ZipFile(src_bbz)
                    alt_zips[src_bbz] = zsrc
                    alt_entries[src_bbz] = {n.lower(): n for n in zsrc.namelist()}
                esrc, where = alt_entries[src_bbz], src_bbz
                print(f'    ↳ {key}: источник переопределён -> {src_bbz}')
            real = esrc.get(key)
            if real is None:
                summary.append((key, 'MISSING', None, None, f'нет в архиве {where}'))
                print(f'  ! {key:34s} MISSING')
                continue
            data = zsrc.read(real)
        res = decode_table(key, data, spec, use_ledgers, dedup_map)
        dedup_total += res.get('dedup_replaced', 0)
        write_outputs(res, write_dir)
        status = 'OK' if res.get('count_ok') else 'CHECK'
        # ведомость хвоста объявлена, но не легла (нет файла/отказ проверок) — это НЕ «OK»: иначе
        # перегенерация молча вернула бы таблицу к архиву и потеряла обогащение.
        if use_ledgers and spec.get('tail_ledger') and not res.get('tail_appended'):
            status = 'CHECK'
        # то же для ведомости значений: хоть одна правка не легла (в архиве не «было») — CHECK.
        if res.get('defect_fixes_expected') and res.get('defect_fixes_applied') != res.get('defect_fixes_expected'):
            status = 'CHECK'
        if use_ledgers and spec.get('value_ledger') \
                and not os.path.isfile(os.path.join(VALUE_LEDGER_DIR, spec['value_ledger'])):
            status = 'CHECK'
        n = res.get('count_used')
        if res.get('tail_appended'):
            print(f'    = {key}: записей архива {n} + хвост {res["tail_appended"]} = {len(res["rows"])}')
        note = spec.get('note', '') + (' | ' + res['note_count'] if 'note_count' in res else '')
        if res.get('dedup_replaced'):
            note += f' | item_dedup: перекодировано {res["dedup_replaced"]}'
        summary.append((key, status, n, res.get('tag'), note.strip(' |')))
        flag = '  ' if status == 'OK' else ' !'
        print(f'{flag}{key:34s} {status:5s} n={n}  tag={res.get("tag")}')

    # непокрытые записи архива
    covered = set(REGISTRY.keys())
    for low, real in entries.items():
        if low in covered: continue
        tag = 'SKIP' if low in SKIP else 'UNREGISTERED'
        summary.append((low, tag, None, None, 'не таблица/не реверсирована'))

    print('\n=== СВОДКА ===')
    ok = sum(1 for s in summary if s[1] == 'OK')
    chk = sum(1 for s in summary if s[1] == 'CHECK')
    print(f'таблиц декодировано OK={ok}  CHECK={chk}  всего в реестре={len(REGISTRY)}')
    print(f'item_dedup: ' + (f'перекодировано ссылок на предметы-дубли {dedup_total} (карта {len(dedup_map)} кодов)'
                             if dedup_map else 'шаг ВЫКЛЮЧЕН (--no-dedup-map)'))
    for s in summary:
        if 'item_dedup:' in (s[4] or ''):
            print(f'    {s[0]:34s} {s[4].split("item_dedup: ")[-1]}')

    # ★★★ 2026-09-25. Серверный оверлей коробок item_list (316) теперь накладывается САМИМ декодером
    #   через ведомость значений `_value_overlay/item_list.csv` (ключ реестра value_ledger) — при
    #   use_ledgers коробки уже в CSV. Отдельный `port_itemdb_union_from_server.py` для них больше не
    #   нужен. ОСТАЛОСЬ два шага после регенерации item_list: восстановить имена и переимпортировать DT.
    #   (Если запущено с --no-ledgers, коробок в CSV НЕТ — так выгружать в боевой каталог нельзя.)
    #
    # ★★★ 2026-09-27 (itemsfix, СРЕДН-2 ревизии items_consistency). В БОЕВОЙ каталог таблицы с именами
    #   больше не выходят «голыми»: декодер САМ накладывает слой-рецепт имён (`asda_apply_name_overlay.py`
    #   — тот же код и та же проверка «изменились только ячейки рецепта») на перегенерированные таблицы.
    #   Раньше это был ручной шаг, и 25.09 вместо него прогнали устаревший asda_fill_item_names.py —
    #   откат 1 559 имён. Вторая линия защиты — гейт импортёра `_import_one_itemdb_table.py`.
    #   В отдельный каталог (--out DIR) слой НЕ накладывается (там нужен чистый декод); наложить явно:
    #   `asda_apply_name_overlay.py --itemdb DIR`.
    name_layer = {'item_list.bin': 'item_list', 'item_tooltip.bin': 'item_tooltip',
                  'sowell_list.bin': 'sowell_list', 'sowell_tooltip.bin': 'sowell_tooltip',
                  'special_itemlist.bin': 'special_itemlist',
                  'specialitem_tooltiplist.bin': 'specialitem_tooltiplist',
                  # ★ 2026-10-14 (names11, этап 2): слой имён и для таблиц с обрезанными/корейскими строками (см. KEY_COLUMN рецепта)
                  'skilltooltip.bin': 'skilltooltip', 'stateinfotip.bin': 'stateinfotip', 'title_option.bin': 'title_option',
                  'specialitem_buftooltiplist.bin': 'specialitem_buftooltiplist', 'event_item_list.bin': 'event_item_list',
                  'soulmateskill.ssi': 'soulmateskill',
                  # ★ 2026-10-14 (names11, этап 4): форматы бонусов питомцев и название навыка 168 (латинская буква-двойник)
                  'petoptiontable.bin': 'petoptiontable', 'skill_info.sif': 'skill_info'}
    to_name = [name_layer[s[0]] for s in summary if s[0] in name_layer and s[1] in ('OK', 'CHECK')]
    name_rc = 0
    if to_name and use_ledgers and live:
        import asda_apply_name_overlay as _NO      # модуль порта (путь добавлен в начале), без сайд-эффектов
        print('\n★★★ слой-рецепт имён -> ' + ','.join(to_name))
        # ★ 2026-09-27 (MELK-2): каталог таблиц передаём ЯВНО (--itemdb), а не полагаемся на NO.ITEMDB.
        #   ★ itemsdatafix: слой накладывается в ПРОМЕЖУТОЧНОМ каталоге (stage), боевой ещё не тронут;
        #   прежний каталог модуля возвращается (МЕЛК-4 ревизии: не оставлять подменённый глобал).
        _prev_itemdb = _NO.ITEMDB
        try:
            name_rc = _NO.main(['--tables', ','.join(to_name), '--itemdb', stage])
        finally:
            _NO.ITEMDB = _prev_itemdb
        if name_rc:
            print(f'  ! слой имён НЕ наложен (код {name_rc}) — эти таблицы в боевой каталог НЕ копируются')

    if live:
        promote_rc = _promote_stage(summary, stage, args.out, name_layer if name_rc else {}, args.accept_diff)
        if promote_rc:
            print('\n!!! Часть таблиц НЕ скопирована в боевой каталог (см. выше). Код 3.')
            sys.exit(3)

    if ('item_list.bin', 'OK') in [(s[0], s[1]) for s in summary] and live:
        if use_ledgers:
            print('\n★★★ item_list.csv перегенерирован. Коробки (316) наложены ведомостью'
                  f' {os.path.join(VALUE_LEDGER_DIR, "item_list.csv")}, имена — слоем-рецептом (выше).'
                  ' Осталось переимпортировать DT:')
            print('    ASDA_TABLES=item_list  run_headless_safe.py AstoryClient\\Tools\\_import_one_itemdb_table.py')
        else:
            print('\n★★★ item_list.csv выгружен с --no-ledgers: коробок (316) и имён В НЁМ НЕТ.'
                  ' В боевой каталог порта так выгружать НЕЛЬЗЯ — перезапусти без --no-ledgers.')
    return summary

if __name__ == '__main__':
    main()
