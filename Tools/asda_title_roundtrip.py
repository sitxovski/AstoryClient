#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""asda_title_roundtrip.py — ПОБАЙТОВАЯ обратная сборка пяти .bin системы званий (Fame/Title).

Задача скрипта: доказать, что формат разобран ПОЛНОСТЬЮ и обратимо. Для каждого файла он
  1) читает оригинал с диска,
  2) декодирует ТЕМ ЖЕ кодом, что `Tools/decode_item_db.py` (модуль импортируется, а не копируется),
  3) СОБИРАЕТ файл обратно из декодированного представления (обратный шифр + MD5-префикс + header),
  4) сравнивает результат с оригиналом БАЙТ В БАЙТ и печатает первые расхождения.

★ MD5-ПРЕФИКС РАСКРЫТ. Эталонный загрузчик `CItemManager_LoadTitleListBin` (мастер-декомпил
  `RE/ida/export/AsdaStory_ida.c`, строка 258553; эталон `RE/etalon/entity_char.c:5734`) считает
      Net_Md5Digest16(&g_NetMd5Salt, &recordBuf[256*i + 16], 0xF0, &Buf2)
  и сравнивает с первыми 16 байтами записи. Внутри `Net_Md5Digest16@0x988450` (network.c:3832)
      Md5Init(ctx); Md5Update(ctx, Src, len); Md5Final(this, ctx, out)
  — то есть первый аргумент (`g_NetMd5Salt`) в вычислении НЕ участвует, это артефакт __thiscall.
  `Md5Init@0x986D90` прошивает стандартные A/B/C/D (0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476).
  ⇒ префикс = ОБЫЧНЫЙ MD5 от ШИФРОВАННОГО тела записи (stride − 16 байт). СОЛИ НЕТ.
  Проверено на всех 5 файлах: 536 записей из 536 сходятся.

★ Шифр строк. Байт текста в файле = байт+1 (CP1251/CP949), «я» (0xFF+1) = 0x00.
  Заполнитель хвоста поля:
    * `title_list.bin`      — 0x01 (после снятия шифра даёт 0x00);
    * `title_option/effect/rank` — 0x00 (старое поколение формата, шифра в хвосте нет).
  Особый случай: `title_list` запись 247, поле Desc (+100) занимает ВСЕ 64 байта без терминатора.

Запуск:
  python asda_title_roundtrip.py [--dir DIR]
Кодов возврата: 0 — все файлы IDENTICAL, 1 — есть расхождения.
"""
import argparse
import hashlib
import os
import struct
import sys

# --- подключение штатного декодера проекта (никаких копий его кода здесь нет) ---
import asda_paths as _P   # ★ гигиена 15.09.2026 (§4 п.2): корни путей — Tools/asda_paths.py (ASDA_ROOT и др.)
_TOOLS = os.path.join(_P.ROOT, "AstoryClient", "Tools", "ItemDB")
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)
import decode_item_db as D                                   # noqa: E402

DEFAULT_DIR = os.path.join(_P.ROOT, "AstoryClient", "RawAssets", "Data", "Character")

# ---------------------------------------------------------------------------
# Описание пяти файлов
#   stride  : шаг записи НА ДИСКЕ (вместе с MD5-префиксом)
#   md5     : длина MD5-префикса (16 у всех пяти; в REGISTRY у title_list_old стоит 0 — это БАГ,
#             см. отчёт `Reports/title_system.md`, раздел «Формат файлов»)
#   dec     : функция дешифровки из decode_item_db (None — шифр не применяется)
#   dwords  : смещения DWORD-полей и константа вычитания (для обратной сборки — прибавления)
#   strings : (смещение, длина, байт-заполнитель хвоста)
# ---------------------------------------------------------------------------
TABLES = [
    dict(name='title_list.bin', stride=256, md5=16, dec=D.dec_title_list,
         dwords=[(o, 40) for o in (0, 4, 8, 12, 80, 84, 88, 92, 96, 164, 168, 172,
                                   176, 180, 184, 188, 192, 196, 200,
                                   204, 208, 212, 216, 220, 224, 228, 232, 236)],
         strings=[(16, 64, 0x01), (100, 64, 0x01)]),
    dict(name='title_option.bin', stride=52, md5=16, dec=D.dec_title_option,
         dwords=[(0, 43)],
         strings=[(4, 32, 0x00)]),
    dict(name='title_effect.bin', stride=276, md5=16, dec=D.dec_title_effect,
         dwords=[(0, 46)],
         strings=[(4 + 32 * k, 32, 0x00) for k in range(8)]),
    dict(name='title_rank.bin', stride=84, md5=16, dec=D.dec_title_rank,
         dwords=[(o, 49) for o in (0, 4, 8, 12, 16, 52, 56, 60, 64)],
         strings=[(20, 32, 0x00)]),
    # ★ title_list_old: загрузчика в клиенте НЕТ ни одного (проверено полным поиском по
    #   мастер-декомпилю: строка "title_list_old" в бинаре не встречается вообще), поэтому
    #   КОНСТАНТЫ ШИФРА НЕДОКАЗУЕМЫ и шифр не применяется — тело записи берётся сырым.
    #   MD5-префикс при этом есть и сходится (38/38).
    dict(name='title_list_old.bin', stride=176, md5=16, dec=None,
         dwords=[], strings=[]),
]


# ---------------------------------------------------------------------------
# Обратный шифр (точное зеркало хелперов decode_item_db)
# ---------------------------------------------------------------------------
def add_dw(buf, off, const):
    """Обратное к `decode_item_db.sub_dw`: возвращает в поле шифрованное значение."""
    v = (struct.unpack_from('<I', buf, off)[0] + const) & 0xFFFFFFFF
    struct.pack_into('<I', buf, off, v)


def enc_str(buf, off, maxlen, pad):
    """Обратное к `decode_item_db.dec_str`.

    `buf` — ДЕШИФРОВАННАЯ запись. Текст идёт до первого 0x00; если 0x00 в поле нет,
    текст занимает поле целиком (так устроена запись 247 в title_list, поле Desc).
    Каждый байт текста увеличивается на 1, хвост поля забивается `pad`.
    """
    n = min(maxlen, len(buf) - off)
    if n <= 0:
        return
    end = n
    for k in range(n):
        if buf[off + k] == 0:
            end = k
            break
    # ★ дедуп №2 В2 (хвост Т5 волны В1): шифр поля — общий `asda_bintable.encipher_field` (терминатор = заполнитель
    #   хвоста этого формата, поле целиком — allow_full). Отличие от прежнего тела — только отказ (ValueError) на
    #   НЕОДНОЗНАЧНОЙ строке: байт текста после шифра совпал с терминатором («я» при заполнителе 0x00), такую строку
    #   декодер обрезал бы — раньше она молча писалась.
    buf[off:off + n] = D._BT.encipher_field(bytes(buf[off:off + end]), n, terminator=pad, allow_full=True)


def encode_record(plain, spec):
    """Плоская дешифрованная запись -> шифрованное тело записи (без MD5-префикса)."""
    b = bytearray(plain)
    for off, ln, pad in spec['strings']:
        enc_str(b, off, ln, pad)
    for off, const in spec['dwords']:
        add_dw(b, off, const)
    return bytes(b)


# ---------------------------------------------------------------------------
def process(path, spec):
    """Полный цикл «прочитать -> декодировать -> собрать -> сравнить» для одного файла."""
    res = dict(name=spec['name'], path=path)
    if not os.path.isfile(path):
        res['error'] = 'файла нет на диске'
        return res
    with open(path, 'rb') as f:
        orig = f.read()
    res['size'] = len(orig)

    ver, count, extra, tag = D.read_header(orig)
    stride, md5len, dec = spec['stride'], spec['md5'], spec['dec']
    body = orig[20:]
    res.update(ver=round(ver, 4), count=count, extra=extra,
               tag=tag.decode('latin-1', 'replace').rstrip('\x00'),
               stride=stride, rem=len(body) % stride, ncalc=len(body) // stride)

    # ---- 1. декод + проверка MD5-префикса ----
    plains, md5_ok = [], 0
    for i in range(count):
        rec = body[i * stride:(i + 1) * stride]
        cipher = rec[md5len:]
        if hashlib.md5(cipher).digest() == rec[:md5len]:
            md5_ok += 1
        b = bytearray(cipher)
        if dec:
            dec(b, i)
        plains.append(bytes(b))
    res['md5_ok'] = md5_ok

    # ---- 2. обратная сборка ----
    out = bytearray()
    out += struct.pack('<f', ver)
    out += struct.pack('<i', count)
    out += struct.pack('<i', extra)
    out += tag
    out += orig[16:20]                      # pad@16 переносим как есть (в header не разобран)
    for i, plain in enumerate(plains):
        cipher = encode_record(plain, spec)
        out += hashlib.md5(cipher).digest()
        out += cipher
    tail = body[count * stride:]            # хвост после объявленных записей (обычно пуст)
    out += tail
    rebuilt = bytes(out)
    res['rebuilt_size'] = len(rebuilt)

    # ---- 3. побайтовое сравнение ----
    diffs = []
    if len(rebuilt) != len(orig):
        diffs.append(('LEN', len(orig), len(rebuilt)))
    for k in range(min(len(rebuilt), len(orig))):
        if rebuilt[k] != orig[k]:
            rec_i = (k - 20) // stride if k >= 20 else -1
            rec_o = (k - 20) % stride if k >= 20 else k
            diffs.append((k, orig[k], rebuilt[k], rec_i, rec_o))
            if len(diffs) >= 40:
                break
    res['diffs'] = diffs
    res['identical'] = (not diffs)
    return res


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description='Побайтовая обратная сборка таблиц системы званий.')
    ap.add_argument('--dir', default=DEFAULT_DIR, help='каталог с .bin (по умолчанию RawAssets/Data/Character)')
    args = ap.parse_args()

    print(f'[dir] {args.dir}')
    bad = 0
    for spec in TABLES:
        path = os.path.join(args.dir, spec['name'])
        r = process(path, spec)
        if 'error' in r:
            print(f"  ! {spec['name']:22s} {r['error']}")
            bad += 1
            continue
        head = (f"  {r['name']:22s} size={r['size']:<7} n={r['count']:<4} tag={r['tag']:<4} "
                f"ver={r['ver']:<7} stride={r['stride']:<4} md5ok={r['md5_ok']}/{r['count']}")
        print(head)
        if r['rem']:
            print(f"      ! остаток {r['rem']} б при stride={r['stride']} (ncalc={r['ncalc']})")
        if r['identical']:
            print('      IDENTICAL — обратная сборка совпала с оригиналом побайтово')
        else:
            bad += 1
            print(f"      РАСХОЖДЕНИЙ: {len(r['diffs'])} (показаны первые)")
            for d in r['diffs'][:20]:
                if d[0] == 'LEN':
                    print(f'        длина: было {d[1]}, стало {d[2]}')
                else:
                    k, o, n, ri, ro = d
                    print(f'        файл+{k} (запись {ri}, смещение записи +{ro}): было 0x{o:02X}, стало 0x{n:02X}')
    print()
    print('ИТОГ: все файлы IDENTICAL' if bad == 0 else f'ИТОГ: проблемных файлов {bad}')
    return 0 if bad == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
