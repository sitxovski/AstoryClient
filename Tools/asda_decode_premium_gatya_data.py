# Copyright (c) 2026 @sitxovski (Discord)
# asda_decode_premium_gatya_data.py — разбор ./Data/premium_gatya_data.bin в CSV для DataTable.
#
# ФОРМАТ УСТАНОВЛЕН ПО КОДУ КЛИЕНТА (`PremiumGatya_LoadData@0xB6F720`, world_map.c:4788-4820),
# а не подбором по байтам:
#     fread(Buffer, 0x14, 1, f);              // заголовок 20 байт
#     count = dataCount;                      // ★ в декомпиле `_BYTE Buffer[4]; int dataCount;` —
#                                             //   соседние локалы, поэтому счётчик = dword @4
#     fread(rows, 8, count, f);               // записи по 8 байт
# Заголовок: @0 i32 · @4 i32 count · @8 i32 · @12 магия "PMD\0" · @16 i32.
# Запись 8 б: @0 i32 slot (индекс автомата) · @4 i32 price (цена в премиум-валюте).
#
# Метод проверен на СОСЕДНЕЙ таблице тем же кодом: у `premium_gatya.bin` (`PremiumGatya_LoadTable`)
# @4 = 106 и тело ровно 106×20 байт, магия "BST".
#
# ⚠ ДЕФЕКТ ДАННЫХ ОРИГИНАЛА: в нашей сборке файл содержит ДВЕ записи ({0,690} и {1,690}),
#   а счётчик @4 = 1 ⇒ оригинал читает только первую (работает один автомат из двух).
#   Скрипт выгружает ВСЕ фактические записи и пишет в лог расхождение.
#
# Запуск: python asda_decode_premium_gatya_data.py <in.bin> <out.csv>
import os, struct, sys

HDR = 20
REC = 8


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else \
        r"@@ASTORY_DEV_ROOT_FWD@@/Donors/AsdaChroniclesInterlude/Data/premium_gatya_data.bin"
    dst = sys.argv[2] if len(sys.argv) > 2 else \
        r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/RawAssets/Data/ItemDB/premium_gatya_data.csv"

    d = open(src, "rb").read()
    if len(d) < HDR:
        print("файл короче заголовка: %d б" % len(d)); return
    h = struct.unpack_from("<5I", d, 0)
    magic = d[12:16].rstrip(b"\0").decode("ascii", "replace")
    count = h[1]
    body = len(d) - HDR
    actual = body // REC

    print("файл: %s (%d б)" % (src, len(d)))
    print("заголовок: %s | магия %r | счётчик @4 = %d" % ([hex(x) for x in h], magic, count))
    print("записей фактически: %d (тело %d б, остаток %d)" % (actual, body, body % REC))
    if magic != "PMD":
        print("⚠ магия не PMD — формат другой, разбор прекращён"); return
    if count != actual:
        print("⚠ ДЕФЕКТ ОРИГИНАЛА: счётчик говорит %d, а записей %d — оригинал прочтёт только %d"
              % (count, actual, count))

    rows = []
    for i in range(actual):
        slot, price = struct.unpack_from("<2i", d, HDR + i * REC)
        rows.append((i, slot, price))
        print("   запись %d: слот=%d цена=%d" % (i, slot, price))

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8", newline="") as f:
        f.write("_row,Slot,Price\n")
        for i, slot, price in rows:
            f.write("%d,%d,%d\n" % (i, slot, price))
    print("→ %s (%d строк)" % (dst, len(rows)))


if __name__ == "__main__":
    main()
