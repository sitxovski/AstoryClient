# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_build_event_item_server_dt.py — СЕРВЕРНАЯ таблица обменов 6098 -> DT_EventItemServer (сторож обмена).

ЗАЧЕМ (27.09.2026, itemsfix, СРЕДН-4 ревизии `Reports/owner_0927/items_consistency_REVIEW.md`).
  Клиент шлёт в 6098 ТОЛЬКО номер строки обмена; что списать и что выдать, сервер берёт из СВОЕЙ
  `ItemTable_Info\EventItem_Table.txt` (`EventItemRequestReq@0x554330`, GameServer.c:130724-130848).
  Таблица RU-клиента (`event_item_list.bin`, линия Chronicles) и серверная (линия Arabic/XYT) в 22
  строках из 68 требуют РАЗНОЕ. Пример: двойной клик по календарю 53648 выбирает строку 5; клиент
  проверяет «53648×40, 53649×25, 53650×5», а сервер по строке 5 спишет 75514×1 и 75462×50 и выдаст
  75523 — если эти предметы у игрока есть. Порт перед 6098 сверяет требования строки клиента с
  требованиями строки СЕРВЕРА и при расхождении запрос не шлёт (`UAsdaNpcDialogSubsystem::
  IsEventExchangeServerSafe`). Эта таблица — источник серверной стороны сверки. Протокол не меняется.

ИСТОЧНИК. Снимок живого файла (только чтение):
  RawAssets/ItemDB/_server_snapshot/EventItem_Table.txt  (md5 5f40ff21…, 25.09.2026 01:03; у
  GameServer и Indun_GameServer файл одинаковый). Обновить снимок — `--refresh-snapshot`
  (копирует с живого сервера, ничего там не пишет). После правки серверной таблицы (решение Q1)
  снимок и DT обновлять обязательно, иначе сторож продолжит закрывать уже исправленные строки.

РАЗБОР — 1:1 с загрузчиком сервера (GameServer.c:69481-69523): первое слово — число строк; строка =
  `idx` (atol) + имя (%s, без пробелов) + 5 пар «код, кол-во» ТРЕБОВАНИЙ + 5 пар НАГРАДЫ; каждое
  поле читается `fscanf("%s")` + `atol` (поэтому «(1)» -> 0). В DT кладём строку по ПОЗИЦИИ i (так
  сервер адресует `base + 190*i`), `Index` = прочитанный `idx` (сервер требует idx == i, иначе
  отказ 6099 код 3), `ItemIds/ItemCounts` = 5 пар требований как есть (сервер идёт по ним до
  первого кода < 0). Награды сторожу не нужны: для безопасности списания важны только требования.

Структура строки — существующая `FAsdaEventItemRow` (Public/Npc/AsdaNpcDialogTypes.h), новой нет.

Запуск:
  python AstoryClient\Tools\asda_build_event_item_server_dt.py --check      # без UE: сверка с клиентом
  python AstoryClient\Tools\asda_build_event_item_server_dt.py --refresh-snapshot
  run_headless_safe.py AstoryClient\Tools\asda_build_event_item_server_dt.py --marker "ЗАВЕРШЕНО"
Побочных эффектов при импорте модуля нет (закон tools-no-import-side-effects).
"""
import csv
import io
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import asda_server_txt as _ST  # noqa: E402  ★ дедуп №2 В2 Т7: путь к живому серверу и чтение .txt как у сервера
PROJECT = os.path.dirname(HERE)
SNAPSHOT = os.path.join(PROJECT, 'RawAssets', 'ItemDB', '_server_snapshot', 'EventItem_Table.txt')
LIVE = _ST.live_itemtable('EventItem_Table.txt')   # ★ дедуп №2 В2 Т7: один путь к живому серверу
CLIENT_CSV = os.path.join(PROJECT, 'RawAssets', 'Data', 'ItemDB', 'event_item_list.csv')
OUT_JSON = os.path.join(PROJECT, 'RawAssets', 'Data', 'NPC', 'EventItemServer.json')
PKG = '/Game/AsdaAssets/Data/NPC'
DT_NAME = 'DT_EventItemServer'
RS_EVENT = '/Script/AsdaReborn.AsdaEventItemRow'


def _atol(s):
    """`atol` CRT сервера — общая эмуляция `asda_server_txt.crt_atol` (★ дедуп №2 В2 Т7).
    Было: регулярка «пробелы, знак, цифры» по строке latin-1 — её классы цифр и пробелов шире CRT (юникодные цифры и пробелы), нет
    насыщения int32). На живой таблице и снимке 11.10.2026 итог тот же."""
    return _ST.crt_atol(s)


def parse_server(path=SNAPSHOT):
    """-> [dict(Index, ItemIds[5], ItemCounts[5], RewardIds[5], RewardCounts[5], Name)] по позиции.
    ★ дедуп №2 В2 Т7: слова — как `fscanf("%s")` сервера (`asda_server_txt.read_tokens`, разделители `isspace` "C");
    прежнее `str.split()` резало ещё и по байтам 0x1C-0x1F, 0x85, 0xA0 (юникодные пробелы)."""
    toks = _ST.read_tokens(path)
    n = _atol(toks[0])
    p = 1
    rows = []
    for i in range(n):
        idx = _atol(toks[p]); name = toks[p + 1]; p += 2
        ids, cnt, rid, rcn = [], [], [], []
        for _ in range(5):
            ids.append(_atol(toks[p])); cnt.append(_atol(toks[p + 1])); p += 2
        for _ in range(5):
            rid.append(_atol(toks[p])); rcn.append(_atol(toks[p + 1])); p += 2
        rows.append(dict(Index=idx, ItemIds=ids, ItemCounts=cnt, RewardIds=rid, RewardCounts=rcn,
                         Name=name.decode('cp949', 'replace')))
    if p != len(toks):
        raise RuntimeError('EventItem_Table: лишние/недостающие поля (%d из %d)' % (p, len(toks)))
    # ★ 01.11.2026 (item_dedup, этап 2): коды-дубли -> основной (общий модуль asda_item_dedup). Серверная таблица после
    #   серверного этапа уже несёт основные коды; здесь — чтобы старый снимок и клиентская сторона сверки (её CSV
    #   перекодирует декодер ItemDB) сравнивались одинаково. ASDA_NO_DEDUP_MAP=1 — как в файле.
    import asda_item_dedup as _DD
    dmap = _DD.active_map()
    for r in rows:
        _DD.remap_keys(r, ('ItemIds', 'RewardIds'), dmap)
    return rows


def server_requirements(r):
    """Что сервер реально потребует и спишет: пары до первого кода < 0 (GameServer.c:130783-130806)."""
    out = []
    for i, c in zip(r['ItemIds'], r['ItemCounts']):
        if i < 0:
            break
        out.append((i, c))
    return sorted(out)


def client_requirements(ids, counts):
    """Что проверяет клиент (`npcExchangeBuyRequest@0xDA3F60`): код < 0 пропускается, «нужно ≤ 0»
    выполняется всегда — такие пары ничего не требуют (строки 30/31: «5, −1»)."""
    return sorted((i, c) for i, c in zip(ids, counts) if i >= 0 and c > 0)


def parse_client(path=CLIENT_CSV):
    import asda_item_dedup as _DD   # ★ 01.11.2026 item_dedup: та же перекодировка, что у серверной стороны сверки
    dmap = _DD.active_map()
    rows = []
    with io.open(path, 'r', encoding='utf-8', newline='') as f:
        for r in csv.DictReader(f):
            b = bytes.fromhex(r['DataHex'])
            rows.append(dict(Index=int(r['Idx']),
                             ItemIds=_DD.remap_list(list(struct.unpack_from('<5i', b, 68)), dmap),
                             ItemCounts=list(struct.unpack_from('<5i', b, 88))))
    return rows


def check():
    srv, cli = parse_server(), parse_client()
    diff = []
    for i in range(max(len(srv), len(cli))):
        s = srv[i] if i < len(srv) else None
        c = cli[i] if i < len(cli) else None
        sr = server_requirements(s) if s else None
        cr = client_requirements(c['ItemIds'], c['ItemCounts']) if c else None
        if sr != cr or (s and s['Index'] != i):
            diff.append(i)
            print('  строка %2d: клиент %s | сервер %s%s' % (i, cr, sr,
                  '' if not s or s['Index'] == i else ' (idx %d != позиции)' % s['Index']))
    print('строк: клиент %d, сервер %d; требования расходятся в %d: %s' % (len(cli), len(srv), len(diff), diff))
    return diff


def dt_json():
    """JSON для DataTable: имя строки = ПОЗИЦИЯ в файле (как адресует сервер), поля FAsdaEventItemRow."""
    import json
    out = [dict(Name=str(i), Index=r['Index'], ItemIds=r['ItemIds'], ItemCounts=r['ItemCounts'])
           for i, r in enumerate(parse_server())]
    return out, json.dumps(out, ensure_ascii=False, indent=0)


def build_in_editor():
    import unreal
    sys.path.insert(0, HERE)
    from asda_dt_common import fill_dt

    def log(m):
        unreal.log_warning('[event-srv] ' + m)

    rows, text = dt_json()
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)
    with open(OUT_JSON, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    dt = fill_dt(unreal, PKG, DT_NAME, RS_EVENT, text, log, fmt='json',
                 missing_hint='Public/Npc/AsdaNpcDialogTypes.h')
    n = len(dt.get_row_names()) if dt else 0
    log('%s: строк %d из %d (снимок %s)' % (DT_NAME, n, len(rows), SNAPSHOT))
    log('ЗАВЕРШЕНО' if dt and n == len(rows) else 'ОШИБКА')


def main(argv):
    if '--refresh-snapshot' in argv:
        os.makedirs(os.path.dirname(SNAPSHOT), exist_ok=True)
        shutil.copy2(LIVE, SNAPSHOT)
        print('снимок обновлён: %s' % SNAPSHOT)
        return 0
    if '--check' in argv:
        check()
        return 0
    build_in_editor()
    return 0


if __name__ == '__main__':
    main(sys.argv[1:])
