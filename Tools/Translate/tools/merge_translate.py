# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
merge_translate.py — перенос ТЕКСТА из русского клиента «Фантазиум» (2012) в клиент
AsdaChroniclesInterlude_MINE строго ПО ID записей, без изменения числовых полей.

Правила (задание пользователя):
  * сверка по ID; замена текста ВСЕГДА, если ID найден в Фантазиуме (даже если в
    оригинале уже русский);
  * всё непокрытое (ID без пары / пустой текст в fanta / таблицы без раскладки) —
    в отдельные файлы-отчёты AstoryClient/Tools/Translate/reports/.

Источник раскладок = @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB\decode_item_db.py (REGISTRY, реверс 1:1) и
AstoryClient/Tools/extract_text_tables.py (loose-таблицы). Никаких выдуманных полей:
меняются только задокументированные текст-поля; ресурс-ссылки (меши/иконки/текстуры)
НЕ трогаются.

Технические факты (проверено в этой сессии):
  * MD5-префикс записей = ЧИСТЫЙ MD5(rec[16:stride]) без соли (Md5Update только по данным).
  * Фантазиум: item_tooltip.bin — легаси-формат stride-16 (без MD5) и БЕЗ шифра.
  * Строки Фантазиума заканчиваются маркером '~' — при переносе один хвостовой '~' снимается.
  * system\error list.bin Фантазиума — ТЕКСТОВЫЙ («ID текст\r\n», CP1251); у MINE — бинарный
    (256Б/запись, шифр byte-100, параллельный массив ID).
Стратегия записи строки: bytes(текст)+NUL по оффсету поля, остальной хвост поля не трогаем
(чтение в клиенте — до первого NUL; безопасно при неизвестной точной длине поля).
"""
import os, sys, struct, hashlib, io

sys.path.insert(0, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB")
import decode_item_db as D

ORIG_CLIENT  = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
FANTA_CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\Fantazium"
T            = r"@@ASTORY_DEV_ROOT@@\Donors\TRANSLATE_unpacked"
ORIG_BBZ_DIR  = os.path.join(T, r"tables_orig\Data\Character")
FANTA_BBZ_DIR = os.path.join(T, r"tables_fanta\data\Character")
OUT      = os.path.join(T, "out_client")
OUT_BBZ  = os.path.join(OUT, "_bbz_entries")     # заменённые entry для репака Bin.bbz
REPORTS  = os.path.join(T, "reports")

for p in (OUT, OUT_BBZ, os.path.join(OUT, "system"), REPORTS,
          os.path.join(REPORTS, "tables"), os.path.join(REPORTS, "loose"),
          os.path.join(REPORTS, "talk")):
    os.makedirs(p, exist_ok=True)

def dec_text(b):
    """cp1251-декод для отчётов."""
    return b.decode("cp1251", "replace")

# --- языковая классификация (защита от деградации перевода) ---
import re
_CYR = re.compile("[а-яА-ЯёЁ]")
_LATL = re.compile("[A-Za-z]")
def lang(b):
    """'rus' | 'lat' (есть лат. буквы) | 'kor' | 'neutral' (цифры/знаки) | 'empty'"""
    if not b.strip():
        return "empty"
    try:
        s949 = b.decode("cp949")
        if any("가" <= ch <= "힣" or "㄰" <= ch <= "㆏" for ch in s949):
            return "kor"
    except UnicodeDecodeError:
        pass
    s = b.decode("cp1251", "replace")
    if _CYR.search(s):
        return "rus"
    if _LATL.search(s):
        return "lat"
    return "neutral"

# ранг: замена разрешена только если ранг fanta НЕ ниже ранга orig
_RANK = {"rus": 3, "lat": 2, "neutral": 2, "kor": 1, "empty": 0}
def may_replace(orig_b, fanta_b):
    return _RANK[lang(fanta_b)] >= _RANK[lang(orig_b)]

def strip_tilde(b):
    return b[:-1] if b.endswith(b"~") else b

def read_cstr(body, off, cap):
    raw = bytes(body[off:off+cap])
    z = raw.find(b"\x00")
    return raw[:z] if z >= 0 else raw

# ---------------------------------------------------------------------------
# 1) BBZ-ТАБЛИЦЫ
# ---------------------------------------------------------------------------
# keymode: 'id' (числовой ключ), 'pos' (позиция записи), 'str' (строковый ключ)
# fields: (имя, off, cap, encrypted(+1))  — off в координатах ДЕШИФРОВАННОГО тела (без MD5)
BBZ_SPECS = {
 'item_list.bin':                 dict(keymode='id', keyoff=0, keytyp='i32', fields=[('ItemName',4,64,True)]),
 'item_tooltip.bin':              dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Description',4,200,True)]),
 'item_upgrade_cost.bin':         dict(keymode='id', keyoff=64, keytyp='i32', fields=[('Name',0,64,False)]),
 'itemoptiontable.bin':           dict(keymode='id', keyoff=0, keytyp='u32', fields=[('FmtString',4,400,False)]),
 'special_itemlist.bin':          dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,32,True)]),
 'specialitem_tooltiplist.bin':   dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,32,True),('Tooltip',36,200,True)]),
 'specialitem_buftooltiplist.bin':dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,32,False),('Desc',36,200,False)]),
 'event_item_list.bin':           dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Str',4,64,False)]),
 'hairtable.bin':                 dict(keymode='id', keyoff=0, keytyp='u16', fields=[('Name',2,32,False),('Description',111,200,False)]),
 'jobtype.bin':                   dict(keymode='pos', fields=[('JobName',0,32,False),('ParentJob',32,32,False)]),
 'sowell_list.bin':               dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,32,True)]),
 'sowell_tooltip.bin':            dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Text',4,200,True)]),
 'title_list.bin':                dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',16,64,True),('Desc',100,64,True)]),
 'title_option.bin':              dict(keymode='id', keyoff=0, keytyp='i32', fields=[('OptionStatName',4,32,True)]),
 'soulmateskilltable.bin':        dict(keymode='id', keyoff=0, keytyp='i32', fields=[('DisplayName',4,32,False),('Text',68,200,False)]),
 'soulsoulmatetext.bin':          dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Text',4,200,False)]),
 'soulmateskill.ssi':             dict(keymode='id', keyoff=0, keytyp='u16', fields=[('Name',2,40,False)]),
 'petexptable.bin':               dict(keymode='id', keyoff=0, keytyp='i32', fields=[('StageName',12,32,False)]),
 'petoptiontable.bin':            dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Format',4,64,False)]),
 'pettable.bin':                  dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,64,False)]),
 'skilltooltip.bin':              dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,32,False),('Text',100,404,False)]),
 'emotiontip.bin':                dict(keymode='str', keyoff=32, keycap=32, fields=[('Name',0,32,False)]),
 'loginhelpertext.bin':           dict(keymode='pos', fields=[('Text',4,200,False)]),
 'stateinfotip.bin':              dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,32,False),('Text',72,200,False)]),
 'soulstateinfotip.bin':          dict(keymode='id', keyoff=0, keytyp='i32', fields=[('Name',4,32,False),('Text',72,200,False)]),
 'skill_info.sif':                dict(keymode='id', keyoff=0, keytyp='u16', fields=[('Name',2,40,False)]),
}

def parse_records(data, stride, md5, noheader):
    """-> (header_bytes, [raw record bytes]) c валидацией размера."""
    if noheader:
        header = b""
        body = data
        n = len(body) // stride
        ok = (len(body) % stride == 0)
        cnt = n
    else:
        header = data[:20]
        cnt = struct.unpack_from('<i', data, 4)[0]
        body = data[20:]
        ok = (len(body) == cnt * stride)
    recs = [body[i*stride:(i+1)*stride] for i in range(cnt)] if ok else None
    return header, recs, cnt, ok

def decoded_body(rec, md5, dec, i):
    b = bytearray(rec[md5:])
    if dec:
        dec(b, i)
    return b

def key_of(body, spec):
    if spec['keymode'] == 'str':
        return read_cstr(body, spec['keyoff'], spec['keycap'])
    return D.rd(body, spec['keyoff'], spec['keytyp'])

def write_str(rec_ba, raw_off, plain_bytes, cap, encrypted):
    """Пишет строку+NUL по оффсету, хвост поля не трогает."""
    pb = plain_bytes[:cap-1]
    out = bytes(((c+1) & 0xFF) for c in pb) if encrypted else pb
    rec_ba[raw_off:raw_off+len(out)+1] = out + b"\x00"

def merge_bbz_table(entry, reg, spec, report_lines, summary):
    op = os.path.join(ORIG_BBZ_DIR, entry)
    fp = os.path.join(FANTA_BBZ_DIR, entry)
    if not os.path.isfile(op):
        summary.append((entry, "NO_ORIG", 0, 0, 0)); return
    if not os.path.isfile(fp):
        summary.append((entry, "NO_FANTA", 0, 0, 0)); return
    od = open(op, "rb").read(); fd = open(fp, "rb").read()
    stride, md5 = reg['stride'], reg.get('md5', 0)
    dec = reg.get('dec'); noheader = reg.get('noheader', False)

    oh, orecs, ocnt, ook = parse_records(od, stride, md5, noheader)
    if not ook:
        summary.append((entry, "ORIG_STRUCT_BAD", ocnt, 0, 0)); return

    # формат fanta: современный (как MINE) или легаси (stride-16, без MD5, без шифра)
    fmt = None
    fh, frecs, fcnt, fok = parse_records(fd, stride, md5, noheader)
    if fok:
        fmt = "same"
    elif md5:
        fh, frecs, fcnt, fok = parse_records(fd, stride - md5, 0, noheader)
        if fok:
            fmt = "legacy"
    if fmt is None:
        summary.append((entry, "FANTA_STRUCT_BAD", ocnt, 0, 0)); return

    # индекс fanta: ключ -> плоское дешифрованное тело
    fmap = {}
    dup = 0
    for i, rec in enumerate(frecs):
        if fmt == "same":
            body = decoded_body(rec, md5, dec, i)
        else:
            body = bytearray(rec)          # легаси: без MD5 и без шифра
        k = i if spec['keymode'] == 'pos' else key_of(body, spec)
        if k in fmap: dup += 1
        else: fmap[k] = body

    out_recs = []
    replaced = missing = empty_skip = 0
    for i, rec in enumerate(orecs):
        obody = decoded_body(rec, md5, dec, i)
        k = i if spec['keymode'] == 'pos' else key_of(obody, spec)
        fbody = fmap.get(k)
        ba = bytearray(rec)
        if fbody is None:
            missing += 1
            cur = " | ".join("%s=%s" % (f[0], dec_text(read_cstr(obody, f[1], f[2]))) for f in spec['fields'])
            report_lines.append("%s\t%s" % (k if spec['keymode'] != 'str' else dec_text(k), cur))
        else:
            did = False
            for (fname, off, cap, enc) in spec['fields']:
                ftext = strip_tilde(read_cstr(fbody, off, cap))
                otext = read_cstr(obody, off, cap)
                if not ftext.strip():
                    if otext.strip():
                        empty_skip += 1
                        report_lines.append("%s\t[FANTA-ПУСТО %s]\t%s" % (
                            k if spec['keymode'] != 'str' else dec_text(k), fname, dec_text(otext)))
                    continue
                if not may_replace(otext, ftext):
                    empty_skip += 1
                    report_lines.append("%s\t[FANTA-ХУЖЕ %s: %s<-%s]\t%s\t=>\t%s" % (
                        k if spec['keymode'] != 'str' else dec_text(k), fname,
                        lang(otext), lang(ftext), dec_text(otext), dec_text(ftext)))
                    continue
                write_str(ba, md5 + off, ftext, cap, enc)
                did = True
            if did:
                replaced += 1
                if md5:
                    ba[0:16] = hashlib.md5(bytes(ba[16:])).digest()
        out_recs.append(bytes(ba))

    outp = os.path.join(OUT_BBZ, entry)
    with open(outp, "wb") as f:
        f.write(oh)
        for r in out_recs:
            f.write(r)
    summary.append((entry, "OK(%s)%s" % (fmt, " dupF=%d" % dup if dup else ""), ocnt, replaced, missing))

# ---------------------------------------------------------------------------
# 2) system\error list.bin  (orig бинарный <- fanta текстовый)
# ---------------------------------------------------------------------------
def merge_error_list(summary):
    op = os.path.join(ORIG_CLIENT, "system", "error list.bin")
    fp = os.path.join(FANTA_CLIENT, "system", "error list.bin")
    od = open(op, "rb").read()
    cnt = struct.unpack_from('<I', od, 0)[0]
    ids_off = 4 + 256 * cnt
    ids = list(struct.unpack_from('<%di' % cnt, od, ids_off))
    # fanta: текст "ID текст\r\n"
    fmap = {}
    for line in open(fp, "rb").read().split(b"\r\n"):
        line = line.strip(b"\n")
        if not line: continue
        sp = line.split(b" ", 1)
        if len(sp) != 2 or not sp[0].isdigit(): continue
        fid = int(sp[0])
        if fid not in fmap:
            fmap[fid] = strip_tilde(sp[1].strip())
    out = bytearray(od)
    replaced = missing = 0
    rep = []
    for i, rid in enumerate(ids):
        o = 4 + 256 * i
        cur_plain = bytes(((b - 100) & 0xFF) for b in od[o:o+256]).split(b"\x00")[0]
        ft = fmap.get(rid)
        if ft is None or not ft.strip():
            missing += 1
            rep.append("%d\t%s" % (rid, dec_text(cur_plain)))
            continue
        if not may_replace(cur_plain, ft):
            missing += 1
            rep.append("%d\t[FANTA-ХУЖЕ %s<-%s]\t%s\t=>\t%s" % (
                rid, lang(cur_plain), lang(ft), dec_text(cur_plain), dec_text(ft)))
            continue
        pb = ft[:255]
        enc = bytes(((b + 100) & 0xFF) for b in pb) + b"\x64" * (256 - len(pb))
        out[o:o+256] = enc
        replaced += 1
    os.makedirs(os.path.join(OUT, "system"), exist_ok=True)
    with open(os.path.join(OUT, "system", "error list.bin"), "wb") as f:
        f.write(bytes(out))
    with io.open(os.path.join(REPORTS, "error_list_missing.txt"), "w", encoding="utf-8-sig") as f:
        f.write("# error list.bin: ID без пары в Фантазиуме (оставлен текст MINE). Всего %d из %d\n" % (missing, cnt))
        f.write("\n".join(rep))
    summary.append(("system/error list.bin", "OK(text->bin) fantaIDs=%d" % len(fmap), cnt, replaced, missing))

# ---------------------------------------------------------------------------
# 3) LOOSE Data\*.BIN (раскладки extract_text_tables.py)
# ---------------------------------------------------------------------------
# (rel, stride, layoutB, keytyp/off, [(имя, off)])  строки плоские, cap = stride-off
LOOSE_SPECS = [
    ("NPC/npc_list.BIN",              136,  False, ('i',0),   [("name",4)]),
    ("NPC/Episode/Episode_Table.BIN", 288,  False, ('i',236), [("name",0)]),
    ("NPC/Raid_quest_data.BIN",       388,  False, ('i',0),   [("name",18)]),
    # ключ = u16@0 (i32@0 захватывал байты имени — проверено дампом обоих клиентов)
    ("NPC/SNQInfo.Bin",               1796, True,  ('H',0),   [("name",2), ("subtitle",34)]),
    ("NPC/NFT.BIN",                   36,   False, ('i',0),   [("family",4)]),
    ("NPC/TIM_List.BIN",              204,  False, ('i',0),   [("village",4)]),
    ("Map_info.BIN",                  392,  False, ('i',0),   [("name",4)]),
    ("Constellation.BIN",             65,   False, ('b',0),   [("name",1)]),
    ("SoulGuarderTable.BIN",          1956, False, ('i',0),   [("name",16)]),
    ("Fish_collection_data.BIN",      348,  False, ('i',0),   [("name",8)]),
    ("Tutorial_text.bin",             204,  False, ('i',0),   [("text",4)]),
    ("Tutorial_text_answer.bin",      204,  False, ('i',0),   [("text",4)]),
    ("Tutorial_middle_category.bin",  204,  False, ('i',0),   [("text",4)]),
    ("NPC/Shop/Bpremium.BIN",         80,   False, ('i',0),   [("name",4)]),
    ("NPC/Shop/Fpremium.BIN",         80,   False, ('i',0),   [("name",4)]),
    ("NPC/Shop/Spremium.bin",         76,   False, ('i',0),   [("name",4)]),
    ("NPC/making_recipe.BIN",         128,  False, ('h',0),   [("name",2)]),
]

def find_ci(root, rel):
    """путь с учётом регистра ФС не важен на Windows; вернуть существующий."""
    p = os.path.join(root, rel)
    return p if os.path.isfile(p) else None

def parse_loose(data, stride, layoutB):
    if len(data) < 20: return None, None, False
    if layoutB:
        cnt = struct.unpack_from('<I', data, 20)[0]; base = 0x18
    else:
        cnt = struct.unpack_from('<I', data, 4)[0]; base = 0x14
    ok = (len(data) - base) == cnt * stride or (len(data) - base) >= cnt * stride
    recs = [data[base+i*stride:base+(i+1)*stride] for i in range(cnt)] if ok else None
    return data[:base], recs, ok

def loose_key(rec, keyspec):
    t, off = keyspec
    if t == 'i': return struct.unpack_from('<i', rec, off)[0]
    if t == 'h': return struct.unpack_from('<h', rec, off)[0]
    if t == 'H': return struct.unpack_from('<H', rec, off)[0]
    if t == 'b': return rec[off]

def merge_loose(summary):
    for rel, stride, lb, keyspec, fields in LOOSE_SPECS:
        base = os.path.splitext(os.path.basename(rel))[0]
        op = find_ci(os.path.join(ORIG_CLIENT, "Data"), rel)
        fpp = find_ci(os.path.join(FANTA_CLIENT, "data"), rel)
        if not op:  summary.append(("loose/"+base, "NO_ORIG", 0, 0, 0)); continue
        if not fpp: summary.append(("loose/"+base, "NO_FANTA", 0, 0, 0)); continue
        od = open(op, "rb").read(); fd = open(fpp, "rb").read()
        ohdr, orecs, ook = parse_loose(od, stride, lb)
        if ook and not orecs:               # у нас таблица пустая (count=0) — переводить нечего
            outp = os.path.join(OUT, "Data", rel.replace("/", os.sep))
            os.makedirs(os.path.dirname(outp), exist_ok=True)
            open(outp, "wb").write(od)
            summary.append(("loose/"+base, "OK(orig-пустая)", 0, 0, 0)); continue
        fhdr, frecs, fok = parse_loose(fd, stride, lb)
        if not ook or not fok:
            summary.append(("loose/"+base, "STRUCT_BAD(o=%s,f=%s)" % (ook, fok), 0, 0, 0)); continue
        fmap = {}
        for rec in frecs:
            k = loose_key(rec, keyspec)
            if k not in fmap: fmap[k] = rec
        rep = []
        out = bytearray(od)
        hbase = len(ohdr)
        replaced = missing = 0
        for i, rec in enumerate(orecs):
            k = loose_key(rec, keyspec)
            f = fmap.get(k)
            if f is None:
                missing += 1
                cur = " | ".join("%s=%s" % (nm, dec_text(read_cstr(rec, off, stride-off))) for nm, off in fields)
                rep.append("%s\t%s" % (k, cur)); continue
            did = False
            for nm, off in fields:
                ft = strip_tilde(read_cstr(f, off, stride-off))
                ot = read_cstr(rec, off, stride-off)
                if not ft.strip():
                    if ot.strip():
                        rep.append("%s\t[FANTA-ПУСТО %s]\t%s" % (k, nm, dec_text(ot)))
                    continue
                if not may_replace(ot, ft):
                    rep.append("%s\t[FANTA-ХУЖЕ %s: %s<-%s]\t%s\t=>\t%s" % (
                        k, nm, lang(ot), lang(ft), dec_text(ot), dec_text(ft)))
                    continue
                o = hbase + i*stride + off
                pb = ft[:stride-off-1]
                out[o:o+len(pb)+1] = pb + b"\x00"
                did = True
            if did: replaced += 1
        outp = os.path.join(OUT, "Data", rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(outp), exist_ok=True)
        with open(outp, "wb") as fo:
            fo.write(bytes(out))
        with io.open(os.path.join(REPORTS, "loose", base + "_missing.txt"), "w", encoding="utf-8-sig") as fo:
            fo.write("# %s: ID без пары в Фантазиуме: %d из %d\n" % (rel, missing, len(orecs)))
            fo.write("\n".join(rep))
        summary.append(("loose/"+base, "OK", len(orecs), replaced, missing))

# ---------------------------------------------------------------------------
# 4) Talk-диалоги NPC/Episode/Talk/*.BIN — позиционно, текст @+5
# ---------------------------------------------------------------------------
def merge_talk(summary):
    import glob
    odir = os.path.join(ORIG_CLIENT, "Data", "NPC", "Episode", "Talk")
    fdir = os.path.join(FANTA_CLIENT, "data", "NPC", "Episode", "Talk")
    if not os.path.isdir(odir) or not os.path.isdir(fdir):
        summary.append(("talk/*", "NO_DIR", 0, 0, 0)); return
    fmapfile = {fn.lower(): fn for fn in os.listdir(fdir)}
    stride = 208
    tot = trep = tmiss = nofile = 0
    rep = []
    for fn in sorted(os.listdir(odir)):
        if not fn.lower().endswith(".bin"): continue
        od = open(os.path.join(odir, fn), "rb").read()
        ffn = fmapfile.get(fn.lower())
        _, orecs, ook = parse_loose(od, stride, False)
        if not ook:
            rep.append("%s\t[СТРУКТУРА ORIG]" % fn); continue
        tot += len(orecs)
        if not ffn:
            nofile += 1
            for i, rec in enumerate(orecs):
                t = read_cstr(rec, 5, stride-5)
                if t.strip(): rep.append("%s:%d\t%s" % (fn, i, dec_text(t)))
            tmiss += len(orecs)
            # файл без пары — копируем как есть
            outp = os.path.join(OUT, "Data", "NPC", "Episode", "Talk", fn)
            os.makedirs(os.path.dirname(outp), exist_ok=True)
            open(outp, "wb").write(od)
            continue
        fd = open(os.path.join(fdir, ffn), "rb").read()
        _, frecs, fok = parse_loose(fd, stride, False)
        if not fok:
            rep.append("%s\t[СТРУКТУРА FANTA]" % fn); continue
        out = bytearray(od)
        for i, rec in enumerate(orecs):
            if i < len(frecs):
                ft = strip_tilde(read_cstr(frecs[i], 5, stride-5))
                if ft.strip() and may_replace(read_cstr(rec, 5, stride-5), ft):
                    o = 0x14 + i*stride + 5
                    pb = ft[:stride-5-1]
                    out[o:o+len(pb)+1] = pb + b"\x00"
                    trep += 1
                    continue
            t = read_cstr(rec, 5, stride-5)
            if t.strip():
                rep.append("%s:%d\t%s" % (fn, i, dec_text(t)))
                tmiss += 1
        outp = os.path.join(OUT, "Data", "NPC", "Episode", "Talk", fn)
        os.makedirs(os.path.dirname(outp), exist_ok=True)
        open(outp, "wb").write(bytes(out))
    with io.open(os.path.join(REPORTS, "talk", "talk_missing.txt"), "w", encoding="utf-8-sig") as fo:
        fo.write("# Talk-диалоги: реплик без пары %d (из %d), файлов без пары: %d\n" % (tmiss, tot, nofile))
        fo.write("\n".join(rep))
    summary.append(("talk/*", "OK файлов-без-пары=%d" % nofile, tot, trep, tmiss))

# ---------------------------------------------------------------------------
# 5) Опись таблиц без раскладки
# ---------------------------------------------------------------------------
def report_unmapped(summary):
    covered_bbz = set(BBZ_SPECS) | set(D.REGISTRY)
    lines = []
    for entry in sorted(os.listdir(ORIG_BBZ_DIR)):
        low = entry.lower()
        if low in D.SKIP or not low.endswith((".bin", ".ssi", ".sif", ".bat")): continue
        if low not in BBZ_SPECS:
            why = "числовая/ресурсная — текста нет" if low in D.REGISTRY else "НЕТ РАСКЛАДКИ"
            lines.append("bbz\t%s\t%s" % (entry, why))
    loose_covered = {s[0].lower() for s in LOOSE_SPECS}
    for root, _, files in os.walk(os.path.join(ORIG_CLIENT, "Data")):
        for fn in files:
            if not fn.lower().endswith(".bin"): continue
            rel = os.path.relpath(os.path.join(root, fn), os.path.join(ORIG_CLIENT, "Data")).replace("\\", "/")
            if rel.lower() in loose_covered: continue
            if rel.lower().startswith("npc/episode/talk/"): continue
            lines.append("loose\t%s\tнет раскладки текст-полей (или таблица без текста)" % rel)
    with io.open(os.path.join(REPORTS, "unmapped_tables.txt"), "w", encoding="utf-8-sig") as f:
        f.write("# Таблицы, НЕ участвовавшие в замене текста, и причина\n")
        f.write("\n".join(lines))
    summary.append(("unmapped-опись", "OK", len(lines), 0, 0))

# ---------------------------------------------------------------------------
# 6) Итоговый скан: что в РЕЗУЛЬТАТЕ осталось не по-русски (kor / латиница с буквами)
# ---------------------------------------------------------------------------
def scan_nonrussian(summary):
    outdir = os.path.join(REPORTS, "untranslated")
    os.makedirs(outdir, exist_ok=True)
    total = 0
    # bbz
    for entry, spec in BBZ_SPECS.items():
        reg = D.REGISTRY.get(entry)
        p = os.path.join(OUT_BBZ, entry)
        if reg is None or not os.path.isfile(p): continue
        stride, md5 = reg['stride'], reg.get('md5', 0)
        dec = reg.get('dec'); noheader = reg.get('noheader', False)
        d = open(p, "rb").read()
        _, recs, cnt, ok = parse_records(d, stride, md5, noheader)
        if not ok: continue
        lines = []
        for i, rec in enumerate(recs):
            body = decoded_body(rec, md5, dec, i)
            k = i if spec['keymode'] == 'pos' else key_of(body, spec)
            for (fname, off, cap, enc) in spec['fields']:
                t = read_cstr(body, off, cap)
                lg = lang(t)
                if lg in ("kor", "lat"):
                    lines.append("%s\t%s\t%s\t%s" % (
                        k if spec['keymode'] != 'str' else dec_text(k), fname, lg, dec_text(t)))
        if lines:
            base = os.path.splitext(entry)[0].replace(" ", "_")
            with io.open(os.path.join(outdir, base + "_nonrussian.txt"), "w", encoding="utf-8-sig") as f:
                f.write("# %s: НЕ-русский текст в ИТОГЕ: %d полей\n" % (entry, len(lines)))
                f.write("\n".join(lines))
            total += len(lines)
    # loose
    for rel, stride, lb, keyspec, fields in LOOSE_SPECS:
        base = os.path.splitext(os.path.basename(rel))[0]
        p = os.path.join(OUT, "Data", rel.replace("/", os.sep))
        if not os.path.isfile(p): continue
        d = open(p, "rb").read()
        _, recs, ok = parse_loose(d, stride, lb)
        if not recs: continue
        lines = []
        for rec in recs:
            k = loose_key(rec, keyspec)
            for nm, off in fields:
                t = read_cstr(rec, off, stride - off)
                lg = lang(t)
                if lg in ("kor", "lat"):
                    lines.append("%s\t%s\t%s\t%s" % (k, nm, lg, dec_text(t)))
        if lines:
            with io.open(os.path.join(outdir, base + "_nonrussian.txt"), "w", encoding="utf-8-sig") as f:
                f.write("# %s: НЕ-русский текст в ИТОГЕ: %d полей\n" % (rel, len(lines)))
                f.write("\n".join(lines))
            total += len(lines)
    # talk
    tdir = os.path.join(OUT, "Data", "NPC", "Episode", "Talk")
    lines = []
    if os.path.isdir(tdir):
        for fn in sorted(os.listdir(tdir)):
            d = open(os.path.join(tdir, fn), "rb").read()
            _, recs, ok = parse_loose(d, 208, False)
            if not recs: continue
            for i, rec in enumerate(recs):
                t = read_cstr(rec, 5, 203)
                lg = lang(t)
                if lg in ("kor", "lat"):
                    lines.append("%s:%d\t%s\t%s" % (fn, i, lg, dec_text(t)))
    if lines:
        with io.open(os.path.join(outdir, "Talk_nonrussian.txt"), "w", encoding="utf-8-sig") as f:
            f.write("# Talk: НЕ-русские реплики в ИТОГЕ: %d\n" % len(lines))
            f.write("\n".join(lines))
        total += len(lines)
    # error list
    p = os.path.join(OUT, "system", "error list.bin")
    lines = []
    if os.path.isfile(p):
        d = open(p, "rb").read()
        cnt = struct.unpack_from('<I', d, 0)[0]
        ids = struct.unpack_from('<%di' % cnt, d, 4 + 256 * cnt)
        for i in range(cnt):
            t = bytes(((b - 100) & 0xFF) for b in d[4+256*i:4+256*i+256]).split(b"\x00")[0]
            lg = lang(t)
            if lg in ("kor", "lat"):
                lines.append("%d\t%s\t%s" % (ids[i], lg, dec_text(t)))
    if lines:
        with io.open(os.path.join(outdir, "error_list_nonrussian.txt"), "w", encoding="utf-8-sig") as f:
            f.write("# error list.bin: НЕ-русские строки в ИТОГЕ: %d\n" % len(lines))
            f.write("\n".join(lines))
        total += len(lines)
    summary.append(("НЕ-русский итог (полей)", "OK -> reports/untranslated", total, 0, 0))

def main():
    summary = []
    # BBZ
    for entry, spec in BBZ_SPECS.items():
        reg = D.REGISTRY.get(entry)
        if reg is None:
            summary.append((entry, "NO_REGISTRY", 0, 0, 0)); continue
        rep_lines = []
        merge_bbz_table(entry, reg, spec, rep_lines, summary)
        base = os.path.splitext(entry)[0].replace(" ", "_")
        with io.open(os.path.join(REPORTS, "tables", base + "_missing.txt"), "w", encoding="utf-8-sig") as f:
            st = summary[-1]
            f.write("# %s: статус=%s, записей=%s, заменено=%s, без пары=%s\n" % (entry, st[1], st[2], st[3], st[4]))
            f.write("\n".join(rep_lines))
    merge_error_list(summary)
    merge_loose(summary)
    merge_talk(summary)
    report_unmapped(summary)
    scan_nonrussian(summary)

    with io.open(os.path.join(REPORTS, "SUMMARY.txt"), "w", encoding="utf-8-sig") as f:
        f.write("%-34s %-28s %8s %9s %8s\n" % ("таблица", "статус", "записей", "заменено", "без пары"))
        for row in summary:
            f.write("%-34s %-28s %8s %9s %8s\n" % row)
    print(open(os.path.join(REPORTS, "SUMMARY.txt"), encoding="utf-8-sig").read())

if __name__ == "__main__":
    main()
