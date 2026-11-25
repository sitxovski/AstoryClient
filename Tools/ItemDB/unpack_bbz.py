#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""
unpack_bbz.py — распаковщик CPackFile-контейнеров AsdaStory (.bbz).

РЕВЕРС (эталон @@ASTORY_DEV_ROOT_ESC@@\\RE\\ida\\export\\AsdaStory_game.c):
  CPackFile_Mount @0x11CE910      — ищет EOCD-сигнатуру "PK\\x05\\x06" с конца,
                                    читает Central Directory (offset/size из EOCD+12).
  CPackFile_GetEntryStream @11CEAF0 — линейно идёт по CD-записям (46 байт заголовок +
                                    nameLen[+28] + extraLen[+30] + commentLen[+32]),
                                    сравнивает имя (strlwr) с искомым.
  CPackFile_OpenEntryDecompress @11CEBC0 — читает Local File Header (30 байт), метод
                                    compMethod==0 (store) или ==8 (deflate) -> zlib inflate raw(-15).
  => КОНТЕЙНЕР = СТАНДАРТНЫЙ ZIP (deflate/store). Штатный zipfile Python читает 1:1.

ШИФРОВАНИЕ ДАННЫХ ВНУТРИ ЗАПИСЕЙ (только item_list.bin, эталон CItemManager_LoadItemList @0x9C7370):
  item_list.bin: заголовок 20 байт, затем N записей по 264 байта на диске:
     [0..15]  MD5(salt g_NetMd5Salt || data[16..264]) — проверка целостности,
     [16..263] = 248 байт полезной ЗАШИФРОВАННОЙ записи ItemRecord.
  Расшифровка ItemRecord (248):
     - перечень DWORD-полей: значение -= 35 (0x23);
     - перечень WORD-полей:  значение -= 35;
     - байты имени [+4..+4+strlen): каждый байт -= 1.
  item_img_table.BIN / прочие BIN: НЕ шифрованы (читаются как есть, stride из кода).

Заголовок BIN (20 байт): [0..3] версия(float) | [4..7] count(int32) |
     [8..11] extra(int32) | [12..15] magic-tag(4 char) | [16..19] pad.

Использование:
  python unpack_bbz.py <Bin.bbz> [--out DIR] [--decode-item-list]
  По умолчанию извлекает ВСЕ записи в DIR (raw, как в ZIP).
  С --decode-item-list дополнительно кладёт РАСШИФРОВАННЫЕ item_list.dec.bin
  (заголовок 20 + N*248 чистых записей) и item_img_table.bin.
"""
import sys, os, struct, zipfile, argparse, hashlib

# --- Соль MD5 (g_NetMd5Salt) ------------------------------------------------
# Эталон: Net_Md5Digest16(&g_NetMd5Salt, data, 0xF8, out) — соль читается из глоба.
# Для нашей задачи (мост itemId->имя) MD5 — только проверка целостности; расшифровка
# полей от соли НЕ зависит. Соль оставляем None => пропускаем md5-верификацию
# (данные всё равно расшифровываются корректно, что подтверждено: rec0 = code 20551 "Gold").
NET_MD5_SALT = None  # bytes | None

# --- Шифро-раскладка ItemRecord(248) (из CItemManager_LoadItemList) ----------
# Индексы DWORD-полей (в 4-байтовых единицах внутри 248-байтовой записи), к которым
# применяется -=35. Скопировано 1:1 из цикла эталона (строки 46313..46361).
ITEM_ENC_DWORD = {0,17,19,20,21,22,23,24,25,27,28,30,31,32,33,35,36,37,38,39,
                  40,41,42,43,45,46,48,49,50,51,53,57,58,54,56,59}
# Индексы WORD-полей (в 2-байтовых единицах), к которым применяется -=35.
ITEM_ENC_WORD  = {36,52,58,68,69,88,94,104,110}
ITEM_STRIDE_DISK = 264   # 16 md5 + 248 data
ITEM_STRIDE_DATA = 248
ITEM_NAME_OFF    = 4     # смещение строки имени внутри 248-байтовой записи
ITEM_NAME_LEN    = 64


def find_entry(z, name):
    low = name.lower()
    for n in z.namelist():
        if n.lower() == low:
            return n
    return None


def read_bin_header(data):
    ver   = struct.unpack('<f', data[0:4])[0]
    count = struct.unpack('<i', data[4:8])[0]
    extra = struct.unpack('<i', data[8:12])[0]
    tag   = data[12:16]
    return ver, count, extra, tag


def decode_item_list(data):
    """Расшифровать item_list.bin -> (count, bytearray(header20 + count*248))."""
    ver, count, extra, tag = read_bin_header(data)
    body = data[20:]
    out = bytearray(data[:20])
    for i in range(count):
        rec = body[ITEM_STRIDE_DISK*i : ITEM_STRIDE_DISK*i + ITEM_STRIDE_DISK]
        if len(rec) < ITEM_STRIDE_DISK:
            break
        payload = bytearray(rec[16:16+ITEM_STRIDE_DATA])
        # DWORD -=35
        for dw in ITEM_ENC_DWORD:
            v = struct.unpack('<i', payload[dw*4:dw*4+4])[0] - 35
            payload[dw*4:dw*4+4] = struct.pack('<i', v)
        # WORD -=35
        for w in ITEM_ENC_WORD:
            v = (struct.unpack('<H', payload[w*2:w*2+2])[0] - 35) & 0xFFFF
            payload[w*2:w*2+2] = struct.pack('<H', v)
        # имя: снятие шифра «байт-1». ДВА ФОРМАТА (как dec_str в decode_item_db.py, RU_MERGE §5):
        #   хвост поля 0x01 → «патченный» формат: дешифруем ПОЛЕ ЦЕЛИКОМ (иначе «я»=0x00 в шифре
        #   обрезает строку: «Реагент уровн» вместо «Реагент уровня D» — юзер 2026-07-29);
        #   хвост 0x00 → исходный формат: до первого нуля шифра.
        if payload[ITEM_NAME_OFF + ITEM_NAME_LEN - 1] == 1:
            for k in range(ITEM_NAME_LEN):
                payload[ITEM_NAME_OFF+k] = (payload[ITEM_NAME_OFF+k] - 1) & 0xFF
        else:
            for k in range(ITEM_NAME_LEN):
                if payload[ITEM_NAME_OFF+k] == 0:
                    break
                payload[ITEM_NAME_OFF+k] = (payload[ITEM_NAME_OFF+k] - 1) & 0xFF
        out += payload
    return count, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bbz')
    ap.add_argument('--out', default=None, help='каталог вывода (по умолч. <bbz>_unpacked)')
    ap.add_argument('--decode-item-list', action='store_true',
                    help='также записать расшифрованный item_list.dec.bin')
    ap.add_argument('--only', default=None, help='извлечь только эти записи (через запятую)')
    args = ap.parse_args()

    outdir = args.out or (os.path.splitext(args.bbz)[0] + '_unpacked')
    os.makedirs(outdir, exist_ok=True)
    z = zipfile.ZipFile(args.bbz)
    only = set(s.strip().lower() for s in args.only.split(',')) if args.only else None

    print(f'[bbz] {args.bbz}  entries={len(z.namelist())}')
    for n in z.namelist():
        if only and n.lower() not in only:
            continue
        d = z.read(n)
        safe = os.path.basename(n)
        with open(os.path.join(outdir, safe), 'wb') as f:
            f.write(d)
        print(f'  extracted {safe:34s} {len(d)} bytes')

    if args.decode_item_list:
        n = find_entry(z, 'item_list.bin')
        if n:
            cnt, dec = decode_item_list(z.read(n))
            p = os.path.join(outdir, 'item_list.dec.bin')
            with open(p, 'wb') as f:
                f.write(dec)
            print(f'[decode] item_list -> {p}  records={cnt} (header20 + {cnt}*248)')
        # img table — без шифра, просто как есть под каноничным именем
        ni = find_entry(z, 'item_img_table.bin')
        if ni:
            p = os.path.join(outdir, 'item_img_table.bin')
            with open(p, 'wb') as f:
                f.write(z.read(ni))
            print(f'[copy]   item_img_table.bin -> {p}')

    print('[done]', outdir)


if __name__ == '__main__':
    main()
