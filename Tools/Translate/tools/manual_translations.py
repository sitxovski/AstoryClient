# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
manual_translations.py — применяет РУЧНЫЕ переводы (те, которых нет ни в одном источнике)
в таблицы клиента.

Формат записи: (файл в Bin.bbz, режим ключа, ключ, поле, русский текст).
Смещения/длины полей и признак шифрования берутся из BBZ_SPECS (merge_translate),
раскладки — из REGISTRY (decode_item_db). MD5-префикс записи пересчитывается, если он есть.

Для пропатченных полей (см. patch_name_len.py) хвост заполняется 0x01, чтобы дешифровка
всего поля дала корректный терминатор; для остальных — нулями, как в оригинале.
"""
import os, io, sys, struct, zipfile, hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB")
import decode_item_db as D
from merge_translate import BBZ_SPECS

CLIENT_BBZ = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\Character\Bin.bbz"

PATCHED = {
    ("item_list.bin", "ItemName"), ("item_tooltip.bin", "Description"),
    ("sowell_list.bin", "Name"), ("sowell_tooltip.bin", "Text"),
    ("special_itemlist.bin", "Name"),
    ("specialitem_tooltiplist.bin", "Name"), ("specialitem_tooltiplist.bin", "Tooltip"),
    ("title_list.bin", "Name"), ("title_list.bin", "Desc"),
}

# --- ручные переводы (от заказчика) -----------------------------------------
# emotiontip: ключ = тег анимации (поле Tag@32), переводится отображаемое имя (Name@0)
MANUAL = [
    ("emotiontip.bin", "tag", "act_angry",     "Name", "Злость"),
    ("emotiontip.bin", "tag", "act_cry",       "Name", "Плачь"),
    ("emotiontip.bin", "tag", "act_juggling",  "Name", "Жонглирование"),
]

def cstr(b, off, cap):
    raw = bytes(b[off:off + cap])
    z = raw.find(b"\x00")
    return raw[:z if z >= 0 else cap]

def encode_field(text_b, cap, encrypted, patched):
    if encrypted:
        body = bytes(((c + 1) & 0xFF) for c in text_b[:cap - 1])
        filler = b"\x01" if patched else b"\x00"
        return body + filler * (cap - len(body))
    return text_b[:cap - 1] + b"\x00" * (cap - len(text_b[:cap - 1]))

def main():
    apply_it = "--apply" in sys.argv
    zin = zipfile.ZipFile(CLIENT_BBZ)
    changed_blobs = {}
    rows = []
    for entry, keymode, key, field, ru in MANUAL:
        reg = D.REGISTRY.get(entry); spec = BBZ_SPECS.get(entry)
        if reg is None or spec is None:
            rows.append((entry, key, field, "НЕТ РАСКЛАДКИ", "")); continue
        stride, md5 = reg["stride"], reg.get("md5", 0)
        data = bytearray(changed_blobs.get(entry, zin.read(entry)))
        cnt = struct.unpack_from("<i", data, 4)[0]
        fmeta = {f[0]: f for f in spec["fields"]}
        if field not in fmeta:
            rows.append((entry, key, field, "НЕТ ПОЛЯ", "")); continue
        _, off, cap, enc = fmeta[field]
        hit = False
        for i in range(cnt):
            o = 20 + i * stride
            if o + stride > len(data): break
            b = bytearray(data[o + md5:o + stride])
            # ключ: для emotiontip это строковый тег в поле Tag@32
            kb = cstr(b, spec.get("keyoff", 32), spec.get("keycap", 32)).decode("cp1251", "replace")
            if kb != key:
                continue
            old = cstr(b, off, cap).decode("cp1251", "replace")
            new = encode_field(ru.encode("cp1251"), cap, enc, (entry, field) in PATCHED)
            b[off:off + cap] = new
            data[o + md5:o + stride] = b
            if md5:
                data[o:o + md5] = hashlib.md5(bytes(b)).digest()
            changed_blobs[entry] = bytes(data)
            rows.append((entry, key, field, old, ru))
            hit = True
            break
        if not hit:
            rows.append((entry, key, field, "КЛЮЧ НЕ НАЙДЕН", ru))

    print("%-18s %-16s %-8s %-22s %s" % ("таблица", "ключ", "поле", "было", "стало"))
    for r in rows:
        print("%-18s %-16s %-8s %-22s %s" % r)

    if not apply_it:
        zin.close(); print("\nсухой прогон. Применить: --apply"); return
    if not changed_blobs:
        zin.close(); print("\nнечего применять"); return
    tmp = CLIENT_BBZ + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            payload = changed_blobs.get(info.filename, zin.read(info))
            zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = info.external_attr
            zout.writestr(zi, payload)
    zin.close()
    os.replace(tmp, CLIENT_BBZ)
    print("\nBin.bbz обновлён (таблиц изменено: %d)" % len(changed_blobs))

if __name__ == "__main__":
    main()
