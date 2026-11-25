# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
repack_names.py — перезаписывает строковые поля шифрованных таблиц ПОЛНЫМИ русскими строками.

Читает исходник Фантазиума по его правилу (подтверждено на 55635/55635 записей item_list):
    имя = поле до первого байта 0x7F ('~' после дешифровки), нули ВНУТРИ поля = буквы 'я' (0xFF)
Пишет в таблицу клиента так, чтобы строку правильно прочитал ПРОПАТЧЕННЫЙ загрузчик
(длина = размер поля целиком):
    поле = encrypt(имя) + 0x01 * (остаток)      # 0x01 после дешифровки даёт 0x00 = терминатор
MD5-префикс записи пересчитывается (проверяется клиентом: `itemlist.bin md5 err`).

Работает по ID: числовые поля клиента не трогаются, меняется только строковое поле.
"""
import os, io, struct, zipfile, hashlib, shutil, sys

CLIENT_BBZ = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\Data\Character\Bin.bbz"
FANTA_DIR  = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked\tables_fanta\data\Character"
REPORTS    = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports"

# entry -> (dstStride, dstMd5, dstDelta, srcStride, srcMd5, srcDelta, srcEncrypted, поля)
# *Delta — сколько вычесть из сырого DWORD-ключа, чтобы получить ID (поле кода тоже шифровано).
# srcEncrypted — зашифровано ли строковое поле В ИСХОДНИКЕ. У Фантазиума item_tooltip лежит
# особняком: без MD5-префикса (stride 204), с нешифрованным ключом И открытым текстом.
TABLES = {
    "item_list.bin":              (264, 16, 35, 264, 16, 35, True,  [(4, 64, "ItemName")]),
    "sowell_list.bin":            (188, 16, 16, 188, 16, 16, True,  [(4, 32, "Name")]),
    "special_itemlist.bin":       (316, 16, 32, 316, 16, 32, True,  [(4, 32, "Name")]),
    "title_list.bin":             (256, 16, 40, 256, 16, 40, True,  [(16, 64, "Name"), (100, 64, "Desc")]),
    "item_tooltip.bin":           (220, 16, 30, 204,  0,  0, False, [(4, 200, "Description")]),
    "sowell_tooltip.bin":         (220, 16, 33, 220, 16, 33, True,  [(4, 200, "Text")]),
    "specialitem_tooltiplist.bin":(252, 16, 36, 252, 16, 36, True,  [(4, 32, "Name"), (36, 180, "Tooltip")]),
}

def read_fanta_string(fld, encrypted=True):
    """Возвращает ЧИСТЫЙ текст (cp1251) из поля исходника.

    encrypted=True  — поле зашифровано «+1» (item_list, sowell_*, special_*, title_list):
                      сначала снимаем шифр, при этом 0x00 внутри поля превращается в 'я' (0xFF).
    encrypted=False — поле лежит открытым текстом (у Фантазиума так хранится item_tooltip):
                      шифр не снимаем, 0x00 здесь настоящий терминатор.
    Конец строки Фантазиум помечает символом '~' (0x7E в чистом виде, 0x7F в зашифрованном).
    """
    if encrypted:
        p = fld.find(b"\x7f")
        raw = fld[:p] if p >= 0 else fld[:fld.find(b"\x00") if b"\x00" in fld else len(fld)]
        return bytes(((b - 1) & 0xFF) for b in raw)
    z = fld.find(b"\x00")
    plain = fld[:z if z >= 0 else len(fld)]
    p = plain.find(b"\x7e")
    return bytes(plain[:p] if p >= 0 else plain)

def encode_field(text, size):
    """text (cp1251 bytes) -> поле size байт: шифр +1, хвост 0x01 (после -1 = 0x00)."""
    body = bytes(((b + 1) & 0xFF) for b in text[:size - 1])
    return body + b"\x01" * (size - len(body))

def load_fanta(entry, srcStride, srcMd5, srcDelta, srcEncrypted, fields):
    p = os.path.join(FANTA_DIR, entry)
    if not os.path.isfile(p): return None
    d = open(p, "rb").read()
    cnt = struct.unpack_from("<i", d, 4)[0]
    out = {}
    for i in range(cnt):
        o = 20 + i * srcStride
        if o + srcStride > len(d): break
        b = d[o + srcMd5:o + srcStride]
        key = (struct.unpack_from("<I", b, 0)[0] - srcDelta) & 0xFFFFFFFF
        vals = {}
        for (fo, fl, nm) in fields:
            vals[nm] = read_fanta_string(b[fo:fo + fl], srcEncrypted)
        out[key] = vals
    return out

def process(entry, spec, zin, report):
    stride, md5, keyDelta, srcStride, srcMd5, srcDelta, srcEncrypted, fields = spec
    src = load_fanta(entry, srcStride, srcMd5, srcDelta, srcEncrypted, fields)
    data = bytearray(zin.read(entry))
    cnt = struct.unpack_from("<i", data, 4)[0]
    if src is None:
        report.append("%s\tнет исходника у Фантазиума" % entry); return bytes(data), (entry, 0, 0, 0, 0)
    n_full = n_same = n_nopair = n_norm = 0
    for i in range(cnt):
        o = 20 + i * stride
        if o + stride > len(data): break
        b = bytearray(data[o + md5:o + stride])
        key = (struct.unpack_from("<I", b, 0)[0] - keyDelta) & 0xFFFFFFFF
        s = src.get(key)
        changed = False
        written = set()          # поля, уже записанные с правильным хвостом
        if s is None:
            n_nopair += 1
        else:
            for (fo, fl, nm) in fields:
                text = s.get(nm, b"")
                if not text.strip():
                    continue
                new = encode_field(text, fl)      # хвост 0x01 проставлен здесь же
                written.add(fo)
                if bytes(b[fo:fo + fl]) != new:
                    b[fo:fo + fl] = new; changed = True
            if changed: n_full += 1
            else:       n_same += 1
        # НОРМАЛИЗАЦИЯ ЗАПОЛНИТЕЛЯ — только для полей, которые мы НЕ переписывали.
        # Пропатченный загрузчик дешифрует поле целиком, поэтому хвост 0x00 превратился бы
        # в 0xFF, т.е. в «яяяя» после текста; заполнитель 0x01 даёт после дешифровки 0x00.
        # ВАЖНО: к переписанным полям это применять НЕЛЬЗЯ — там 0x00 внутри строки означает
        # букву 'я' (0xFF+1), и обрезка по первому нулю снова укоротила бы имя.
        for (fo, fl, nm) in fields:
            if fo in written:
                continue
            fld = b[fo:fo + fl]
            z = fld.find(b"\x00")
            if z < 0:
                continue
            if bytes(fld[z:]) != b"\x01" * (fl - z):
                b[fo + z:fo + fl] = b"\x01" * (fl - z)
                changed = True
                n_norm += 1
        if changed:
            data[o + md5:o + stride] = b
            data[o:o + md5] = hashlib.md5(bytes(b)).digest()   # пересчёт MD5 записи
    return bytes(data), (entry, n_full, n_same, n_nopair, n_norm)

def main():
    apply_it = "--apply" in sys.argv
    os.makedirs(REPORTS, exist_ok=True)
    zin = zipfile.ZipFile(CLIENT_BBZ)
    report = []
    rows = []
    newdata = {}
    for entry, spec in TABLES.items():
        if entry not in zin.namelist():
            report.append("%s\tнет в Bin.bbz клиента" % entry); continue
        blob, row = process(entry, spec, zin, report)
        newdata[entry] = blob
        rows.append(row)
    print("%-28s %10s %10s %10s %12s" % ("таблица", "обновлено", "без изм.", "нет пары", "заполнитель"))
    for r in rows:
        print("%-28s %10d %10d %10d %12d" % r)

    if not apply_it:
        zin.close()
        print("\nсухой прогон. Применить: --apply")
        return
    tmp = CLIENT_BBZ + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            payload = newdata.get(info.filename, None)
            if payload is None:
                payload = zin.read(info)
            zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = info.external_attr
            zout.writestr(zi, payload)
    zin.close()
    os.replace(tmp, CLIENT_BBZ)
    print("\nBin.bbz перезаписан")
    with io.open(os.path.join(REPORTS, "repack_names.txt"), "w", encoding="utf-8-sig") as f:
        f.write("\n".join(report))

if __name__ == "__main__":
    main()
