# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
fix_missed.py — доперенос переводов, пропущенных из-за ошибки определителя языка.

Первая версия слияния использовала детектор, который проверял cp949 первым и помечал русский
текст как корейский («Бумибуми» → валидные слоги cp949). Правило «заменять, только если язык
источника не хуже» из-за этого запрещало замену английского текста русским. Здесь проход
повторяется с исправленным детектором (langdet): поле заменяется, если в клиенте оно НЕ русское,
а в Фантазиуме — русское.

Покрывает свободные таблицы (Data/**) и таблицы Bin.bbz. Учитывает шифрование полей, маркер
конца строки Фантазиума (0x7F в зашифрованных, 0x7E в открытых), заполнитель 0x01 для
пропатченных полей и пересчёт MD5 записи.
"""
import os, io, sys, struct, zipfile, hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB")
import decode_item_db as D
from merge_translate import BBZ_SPECS, LOOSE_SPECS, parse_loose, loose_key
from langdet import lang

CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
FANTA = r"@@ASTORY_DEV_ROOT@@\Donors\Fantazium"
FANTA_BBZ_DIR = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked\tables_fanta\data\Character"
REPORT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\fix_missed.txt"

PATCHED = {
    ("item_list.bin", "ItemName"), ("item_tooltip.bin", "Description"),
    ("sowell_list.bin", "Name"), ("sowell_tooltip.bin", "Text"),
    ("special_itemlist.bin", "Name"),
    ("specialitem_tooltiplist.bin", "Name"), ("specialitem_tooltiplist.bin", "Tooltip"),
    ("title_list.bin", "Name"), ("title_list.bin", "Desc"),
}
# у Фантазиума эта таблица лежит открытым текстом и без MD5 (stride 204, ключ без сдвига)
SRC_OVERRIDE = {"item_tooltip.bin": dict(stride=204, md5=0, encrypted=False)}

def cstr(b, off, cap):
    raw = bytes(b[off:off + cap])
    z = raw.find(b"\x00")
    return raw[:z if z >= 0 else cap]

def read_client(body, off, cap, enc, patched):
    raw = bytes(body[off:off + cap])
    if not enc:
        return cstr(body, off, cap)
    if patched:
        plain = bytes(((c - 1) & 0xFF) for c in raw)
        z = plain.find(b"\x00")
        return plain[:z if z >= 0 else cap]
    z = raw.find(b"\x00")
    return bytes(((c - 1) & 0xFF) for c in (raw[:z] if z >= 0 else raw))

def read_src(body, off, cap, enc):
    """Чистый текст из исходника; конец помечен '~' (0x7F в шифре, 0x7E открытым)."""
    raw = bytes(body[off:off + cap])
    if enc:
        p = raw.find(b"\x7f")
        part = raw[:p] if p >= 0 else (raw[:raw.find(b"\x00")] if b"\x00" in raw else raw)
        return bytes(((c - 1) & 0xFF) for c in part)
    z = raw.find(b"\x00")
    plain = raw[:z if z >= 0 else cap]
    p = plain.find(b"\x7e")
    return plain[:p] if p >= 0 else plain

def write_field(b, off, cap, enc, patched, text):
    """Записать строку в поле ИЗВЕСТНОЙ длины cap (только для таблиц Bin.bbz, где cap взят
    из проверенной раскладки). Заполняет поле целиком — допустимо, т.к. cap не выходит
    за границу поля."""
    if enc:
        body = bytes(((c + 1) & 0xFF) for c in text[:cap - 1])
        filler = b"\x01" if patched else b"\x00"
        b[off:off + cap] = body + filler * (cap - len(body))
    else:
        t = text[:cap - 1]
        b[off:off + cap] = t + b"\x00" * (cap - len(t))

def avail_space(b, off, limit):
    """Сколько байт реально доступно под строку по смещению off: длина текущей строки плюс
    идущие сразу за её терминатором нули. Нужно для СВОБОДНЫХ таблиц, где длина текстового
    поля в раскладке не указана, а сразу за ним идут числовые поля (модель, анимация NPC).
    Запись «до конца записи» их затирала — клиент терял модель и ругался '_0.jbb'."""
    end = off
    while end < off + limit and b[end] != 0:
        end += 1
    z = end
    while z < off + limit and b[z] == 0:
        z += 1
    return (end - off) + (z - end)      # символы строки + свободные нули

def write_field_safe(b, off, limit, text):
    """Запись в свободной таблице: НЕ выходим за доступное место, ставим один терминатор."""
    space = avail_space(b, off, limit)
    if len(text) + 1 > space:
        return False
    b[off:off + len(text)] = text
    b[off + len(text)] = 0
    return True

def key_of(body, spec):
    km = spec["keymode"]
    if km == "str":
        return cstr(body, spec["keyoff"], spec.get("keycap", 32)).decode("cp1251", "replace")
    fmt = {"i32": "<i", "u32": "<I", "u16": "<H", "i16": "<h"}[spec.get("keytyp", "i32")]
    return struct.unpack_from(fmt, body, spec.get("keyoff", 0))[0]

def fix_loose(log, skipped):
    total = 0
    for rel, stride, lb, keyspec, fields in LOOSE_SPECS:
        cp = os.path.join(CLIENT, "Data", rel.replace("/", os.sep))
        fp = os.path.join(FANTA, "data", rel.replace("/", os.sep))
        if not (os.path.isfile(cp) and os.path.isfile(fp)):
            continue
        cd = bytearray(open(cp, "rb").read()); fd = open(fp, "rb").read()
        _, crecs, cok = parse_loose(bytes(cd), stride, lb)
        _, frecs, fok = parse_loose(fd, stride, lb)
        if not crecs or not frecs:
            continue
        base = 0x18 if lb else 0x14
        fmap = {}
        for r in frecs:
            fmap[loose_key(r, keyspec)] = r
        n = 0
        for i, rec in enumerate(crecs):
            k = loose_key(rec, keyspec)
            src = fmap.get(k)
            if src is None:
                continue
            b = bytearray(rec)
            hit = False
            for nm, off in fields:
                cur = cstr(b, off, stride - off)
                new = cstr(src, off, stride - off)
                p = new.find(b"\x7e")
                if p >= 0: new = new[:p]
                if lang(cur) != "rus" and lang(new) == "rus":
                    if not write_field_safe(b, off, stride - off, new):
                        skipped.append("%s\t%s\t%s\tНЕ ВЛЕЗАЕТ (%d б доступно)\t%s" % (
                            rel, k, nm, avail_space(b, off, stride - off),
                            new.decode("cp1251", "replace")))
                        continue
                    hit = True
                    log.append("%s\t%s\t%s\t%s\t%s" % (rel, k, nm,
                               cur.decode("cp1251", "replace"), new.decode("cp1251", "replace")))
            if hit:
                cd[base + i * stride:base + (i + 1) * stride] = b
                n += 1
        if n:
            open(cp, "wb").write(bytes(cd))
            total += n
            print("  %-34s исправлено записей: %d" % (rel, n))
    return total

def fix_bbz(log):
    zin = zipfile.ZipFile(os.path.join(CLIENT, "Data", "Character", "Bin.bbz"))
    blobs = {}
    total = 0
    for entry, spec in BBZ_SPECS.items():
        reg = D.REGISTRY.get(entry)
        sp = os.path.join(FANTA_BBZ_DIR, entry)
        if reg is None or entry not in zin.namelist() or not os.path.isfile(sp):
            continue
        stride, md5 = reg["stride"], reg.get("md5", 0)
        noheader = reg.get("noheader", False)
        ov = SRC_OVERRIDE.get(entry, {})
        s_stride, s_md5 = ov.get("stride", stride), ov.get("md5", md5)
        s_enc_ov = ov.get("encrypted", None)
        data = bytearray(zin.read(entry)); sd = open(sp, "rb").read()
        cbase = 0 if noheader else 20
        ccnt = (len(data) - cbase) // stride if noheader else struct.unpack_from("<i", data, 4)[0]
        scnt = (len(sd) - (0 if noheader else 20)) // s_stride if noheader else struct.unpack_from("<i", sd, 4)[0]
        sbase = 0 if noheader else 20
        smap = {}
        for i in range(scnt):
            o = sbase + i * s_stride
            if o + s_stride > len(sd): break
            body = sd[o + s_md5:o + s_stride]
            smap[i if spec["keymode"] == "pos" else key_of(body, spec)] = body
        n = 0
        for i in range(ccnt):
            o = cbase + i * stride
            if o + stride > len(data): break
            b = bytearray(data[o + md5:o + stride])
            k = i if spec["keymode"] == "pos" else key_of(b, spec)
            src = smap.get(k)
            if src is None:
                continue
            hit = False
            for (fname, off, cap, enc) in spec["fields"]:
                patched = (entry, fname) in PATCHED
                senc = enc if s_enc_ov is None else s_enc_ov
                cur = read_client(b, off, cap, enc, patched)
                new = read_src(src, off, cap, senc)
                if lang(cur) != "rus" and lang(new) == "rus":
                    write_field(b, off, cap, enc, patched, new)
                    hit = True
                    log.append("%s\t%s\t%s\t%s\t%s" % (entry, k, fname,
                               cur.decode("cp1251", "replace"), new.decode("cp1251", "replace")))
            if hit:
                data[o + md5:o + stride] = b
                if md5:
                    data[o:o + md5] = hashlib.md5(bytes(b)).digest()
                n += 1
        if n:
            blobs[entry] = bytes(data); total += n
            print("  %-34s исправлено записей: %d" % (entry, n))
    if blobs:
        p = os.path.join(CLIENT, "Data", "Character", "Bin.bbz")
        tmp = p + ".tmp"
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                payload = blobs.get(info.filename, zin.read(info))
                zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                zi.compress_type = zipfile.ZIP_DEFLATED
                zi.external_attr = info.external_attr
                zout.writestr(zi, payload)
        zin.close()
        os.replace(tmp, p)
    else:
        zin.close()
    return total

def main():
    log = []
    skipped = []
    only = "--loose-only" in sys.argv
    print("СВОБОДНЫЕ ТАБЛИЦЫ:")
    a = fix_loose(log, skipped)
    b = 0
    if not only:
        print("ТАБЛИЦЫ Bin.bbz:")
        b = fix_bbz(log)
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with io.open(REPORT, "w", encoding="utf-8-sig") as f:
        f.write("# Доперенесённые переводы (пропущены из-за ошибки определителя языка)\n")
        f.write("# таблица\tключ\tполе\tбыло\tстало\n")
        f.write("\n".join(log))
        if skipped:
            f.write("\n\n# ПРОПУЩЕНО — перевод не влезает в доступное место поля\n")
            f.write("\n".join(skipped))
    print("\nисправлено записей: свободные %d, Bin.bbz %d | всего полей %d | пропущено %d"
          % (a, b, len(log), len(skipped)))
    print("отчёт: %s" % REPORT)

if __name__ == "__main__":
    main()
