# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_title_sources.py — ЕДИНЫЙ ЗАГРУЗЧИК всех известных источников таблицы званий (Fame/Title).

Зачем отдельный модуль: источников шесть, и у каждого своя мелочь, на которой легко ошибиться —
разный терминатор строк, разная кодировка, у одного вообще нет ни MD5, ни шифра. Один раз
разобрались — дальше все скрипты слияния пользуются этим модулем.

★★★ ГЛАВНАЯ ГРАБЛЯ (закон `item-string-cipher-terminator`). Строки шифрованы «байт − 1» и лежат
в однобайтовой кодировке. В ШИФРОВАННОМ виде байт `0x00` — это буква «я» (CP1251 0xFF + 1), а НЕ
конец строки. Настоящий терминатор у РАЗНЫХ поколений файла РАЗНЫЙ:

    наш RawAssets / out_client          -> 0x01
    Donors/TRANSLATE_unpacked/tables_fanta              -> 0x7F   (★ проверено побайтово)
    Donors/TRANSLATE_unpacked/tables_orig (английский)  -> 0x00   (в английском тексте «я» не бывает, конфликта нет)
    Data/NPC/title_list.BIN             -> строки ВООБЩЕ НЕ ШИФРОВАНЫ, терминатор 0x00

Прочитать fanta-файл по правилу `0x01` — значит получить текст, обрезанный на первой «я»
(«Поиск члена отр» вместо полной фразы). Именно так и появляются «битые описания».

Побочных эффектов при импорте нет (закон `tools-no-import-side-effects`).
"""
import os
import struct
import sys

import asda_bintable as _BT   # общий шифр строк и терминаторы (волна 2 аудита дублей, D8)

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# ── пути источников ──────────────────────────────────────────────────────────
ROOT = r'@@ASTORY_DEV_ROOT@@'
P_OURS   = os.path.join(ROOT, r'AstoryClient\RawAssets\Data\Character\title_list.bin')
P_CSV    = os.path.join(ROOT, r'AstoryClient\RawAssets\Data\ItemDB\title_list.csv')
P_ORIG   = os.path.join(ROOT, r'Donors\TRANSLATE_unpacked\tables_orig\Data\Character\title_list.bin')
P_FANTA  = os.path.join(ROOT, r'Donors\TRANSLATE_unpacked\tables_fanta\data\Character\title_list.bin')
P_ARAP   = os.path.join(ROOT, 'Donors', 'ARap', 'Data', 'Character', 'title_list.BIN')
P_NPC    = os.path.join(ROOT, r'Donors\AsdaChroniclesInterlude_MINE\Data\NPC\title_list.BIN')
P_ARNPC  = os.path.join(ROOT, r'Donors\ArabicASDAStory\data\NPC\title_list.BIN')
P_SERVER = os.path.join(ROOT, 'Donors', 'GameServer_stock', 'ConfigSet', 'FameInfo.txt')

# ── раскладка записи (240 б, дешифрованная) ──────────────────────────────────
DW_OFFSETS = (0, 4, 8, 12, 80, 84, 88, 92, 96, 164, 168, 172,
              176, 180, 184, 188, 192, 196, 200,
              204, 208, 212, 216, 220, 224, 228, 232, 236)
DW_CONST = 40
STR_FIELDS = ((16, 64), (100, 64))       # (смещение, длина): Name, Desc

OFF_ID, OFF_SORT, OFF_CLASS = 0, 4, 12
OFF_NAME, OFF_GRADE, OFF_GROUP = 16, 80, 84
OFF_POS, OFF_CAT, OFF_POINT, OFF_DESC = 88, 92, 96, 100
OFF_ENABLED = 200

# Предел загрузчика серверной таблицы: sub_4A18F0@0x4A18F0 отвергает count > 256 целиком.
SERVER_MAX_ROWS_HINT = 256


def _decode_field(raw, terminator, encoding, ciphered):
    """Расшифровать одно строковое поле с УЧЁТОМ его терминатора.

    `terminator` — байт-терминатор В ШИФРОВАННОМ виде (0x01 / 0x7F / 0x00).
    Если терминатора в поле нет вовсе — текст занимает поле ЦЕЛИКОМ (и, значит, обрезан).
    Шифр, терминатор и декод — общий `asda_bintable.decipher_field` (волна 2, D8).
    """
    body, full = _BT.decipher_field(raw, terminator, ciphered)
    return _BT.decode_text(body, encoding).rstrip('\x00'), full


def load_bin(path, terminator, encoding='cp1251', md5=16, ciphered=True):
    """Прочитать title_list-подобный .bin. Возвращает {id: {...}}."""
    with open(path, 'rb') as f:
        data = f.read()
    count = struct.unpack_from('<i', data, 4)[0]
    stride = md5 + 240
    body = data[20:]
    out = {}
    for i in range(count):
        rec = body[i * stride:(i + 1) * stride]
        if len(rec) < stride:
            break
        payload = bytearray(rec[md5:])
        if ciphered:
            for o in DW_OFFSETS:
                v = (struct.unpack_from('<I', payload, o)[0] - DW_CONST) & 0xFFFFFFFF
                struct.pack_into('<I', payload, o, v)
        name, name_full = _decode_field(bytes(payload[OFF_NAME:OFF_NAME + 64]),
                                        terminator, encoding, ciphered)
        desc, desc_full = _decode_field(bytes(payload[OFF_DESC:OFF_DESC + 64]),
                                        terminator, encoding, ciphered)
        rid = struct.unpack_from('<i', payload, OFF_ID)[0]
        out[rid] = {
            'row': i, 'Id': rid,
            'SortNum': struct.unpack_from('<i', payload, OFF_SORT)[0],
            'ClassReq': struct.unpack_from('<i', payload, OFF_CLASS)[0],
            'Name': name, 'NameFull': name_full,
            'ColorIndex': struct.unpack_from('<i', payload, OFF_GRADE)[0],
            'Group': struct.unpack_from('<i', payload, OFF_GROUP)[0],
            'Position': struct.unpack_from('<i', payload, OFF_POS)[0],
            'CategoryLocIndex': struct.unpack_from('<i', payload, OFF_CAT)[0],
            'Point': struct.unpack_from('<i', payload, OFF_POINT)[0],
            'Desc': desc, 'DescFull': desc_full,
            'payload': bytes(payload),
        }
    return out


def load_server(path=P_SERVER):
    """Серверная FameInfo.txt: {id: [колонки как строки]} + сырые строки."""
    with open(path, 'rb') as f:
        raw = f.read().decode('cp1251')
    lines = raw.split('\n')
    rows = {}
    for i, line in enumerate(lines):
        if i == 0:
            continue
        line = line.rstrip('\r')
        if not line.strip():
            continue
        parts = line.split('\t')
        if len(parts) < 2:
            continue
        rows[int(parts[0])] = parts
    return rows, lines


def load_all():
    """Все источники разом. Отсутствующий файл просто пропускается."""
    src = {}
    spec = [
        ('ours',  P_OURS,  0x01, 'cp1251', 16, True),    # наш клиент, RU
        ('orig',  P_ORIG,  0x00, 'cp1251', 16, True),    # английский оригинал
        ('fanta', P_FANTA, 0x7F, 'cp1251', 16, True),    # перевод Fantazium
        # ★ 14.09.2026 (волна 2, D8): у арабской сборки терминатора 0x01 в полях НЕТ — при 0x01 текст шёл
        #   целиком с хвостом-заполнителем «ےےےے» (70 имён, 60 описаний). Верен режим AUTO (0x01, иначе 0x00).
        ('arap',  P_ARAP,  _BT.TERM_AUTO, 'cp1256', 16, True),    # арабская сборка
        ('npc',   P_NPC,   0x00, 'cp1251', 0,  False),   # старый нешифрованный, EN
        ('arnpc', P_ARNPC, 0x00, 'cp1256', 0,  False),   # старый нешифрованный, арабский клиент
    ]
    for name, path, term, enc, md5, ciph in spec:
        if os.path.isfile(path):
            try:
                src[name] = load_bin(path, term, enc, md5, ciph)
            except Exception as exc:
                src[name] = {}
                print('  ! %s: не прочитан (%s)' % (name, exc))
    return src


def main():
    src = load_all()
    print('ИСТОЧНИКИ ТАБЛИЦЫ ЗВАНИЙ')
    for name, recs in src.items():
        ids = sorted(recs)
        named = sum(1 for r in recs.values() if r['Name'] and r['Name'] != '-')
        cut = sum(1 for r in recs.values() if r['DescFull'])
        print('  %-6s записей %3d  id %3d..%-3d  с именем %3d  описание заняло всё поле %3d'
              % (name, len(recs), ids[0], ids[-1], named, cut))
    rows, _ = load_server()
    print('  %-6s записей %3d  (только числа, имён нет)' % ('server', len(rows)))
    for name in ('ours', 'orig', 'fanta', 'arap', 'npc'):
        if name in src and 0 in src[name]:
            r = src[name][0]
            print('\n  %s / id 0: имя %r' % (name, r['Name']))
            print('      описание %r' % (r['Desc'],))


if __name__ == '__main__':
    main()
