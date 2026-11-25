# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
verify_translate.py — проверка результата merge_translate.py:
  1. Размер/число записей выхода == оригиналу.
  2. Диффы байтов ТОЛЬКО в разрешённых зонах: [0..16) MD5-префикс (если есть)
     и зоны текст-полей [off .. конец поля/страйда]. Любой дифф вне зон = FAIL.
  3. MD5 каждой записи выходных MD5-таблиц валиден (plain MD5(rec[16:])).
  4. Выборочный декод: строки должны читаться (CP1251), печать примеров.
"""
import os, sys, struct, hashlib

sys.path.insert(0, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB")
import decode_item_db as D
sys.path.insert(0, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\tools")
from merge_translate import (BBZ_SPECS, LOOSE_SPECS, ORIG_BBZ_DIR, OUT_BBZ, OUT,
                             ORIG_CLIENT, parse_records, parse_loose, decoded_body, read_cstr)

fails = []

def check_zones(orig, new, stride, recbase, nrec, zones, label):
    """zones = [(start,end)] в координатах записи (raw)."""
    if len(orig) != len(new):
        fails.append("%s: РАЗМЕР %d != %d" % (label, len(orig), len(new))); return
    if orig[:recbase] != new[:recbase]:
        fails.append("%s: ИЗМЕНЁН ЗАГОЛОВОК" % label)
    for i in range(nrec):
        b0 = recbase + i * stride
        o = orig[b0:b0+stride]; n = new[b0:b0+stride]
        if o == n: continue
        for p in range(stride):
            if o[p] != n[p] and not any(s <= p < e for s, e in zones):
                fails.append("%s: rec %d дифф вне зон @+0x%X" % (label, i, p))
                return

def field_end(off, fields_sorted, stride):
    """конец зоны поля = начало следующего известного поля либо stride."""
    nxt = [f for f in fields_sorted if f > off]
    return min(nxt) if nxt else stride

print("=== BBZ-таблицы ===")
for entry, spec in BBZ_SPECS.items():
    reg = D.REGISTRY[entry]
    stride, md5 = reg['stride'], reg.get('md5', 0)
    noheader = reg.get('noheader', False)
    op = os.path.join(ORIG_BBZ_DIR, entry); np_ = os.path.join(OUT_BBZ, entry)
    if not os.path.isfile(np_): print("  - %s: нет выхода (пропущена)" % entry); continue
    od = open(op, "rb").read(); nd = open(np_, "rb").read()
    oh, orecs, ocnt, ook = parse_records(od, stride, md5, noheader)
    zones = [(0, md5)] if md5 else []
    # зоны текст-полей в raw-координатах: конец зоны — до следующего известного поля колонок REGISTRY
    known_offs = sorted(set(md5 + c[0] for c in reg['cols']))
    for (fname, off, cap, enc) in spec['fields']:
        raw_off = md5 + off
        end = min(md5 + off + cap, field_end(raw_off, known_offs, stride))
        zones.append((raw_off, max(end, raw_off + 1)))
    recbase = 0 if noheader else 20
    check_zones(od, nd, stride, recbase, ocnt, zones, entry)
    # MD5 всех записей выхода
    if md5:
        badmd5 = 0
        for i in range(ocnt):
            rec = nd[recbase+i*stride : recbase+(i+1)*stride]
            if hashlib.md5(rec[16:]).digest() != rec[:16]: badmd5 += 1
        if badmd5: fails.append("%s: MD5 битый у %d записей" % (entry, badmd5))
    # выборочный декод
    _, nrecs, _, _ = parse_records(nd, stride, md5, noheader)
    sample = []
    dec = reg.get('dec')
    for i in (0, 1, min(100, ocnt-1)):
        body = decoded_body(nrecs[i], md5, dec, i)
        f0 = spec['fields'][0]
        sample.append(read_cstr(body, f0[1], f0[2]).decode('cp1251', 'replace'))
    print("  OK %-32s пример: %s" % (entry, " | ".join(s[:28] for s in sample)))

print("=== error list.bin ===")
od = open(os.path.join(ORIG_CLIENT, "system", "error list.bin"), "rb").read()
nd = open(os.path.join(OUT, "system", "error list.bin"), "rb").read()
cnt = struct.unpack_from('<I', nd, 0)[0]
assert len(nd) == len(od) == 4 + cnt*256 + cnt*4, "размер error list"
assert od[4+256*cnt:] == nd[4+256*cnt:], "массив ID изменён!"
ids = list(struct.unpack_from('<%di' % cnt, nd, 4 + 256*cnt))
idx = {v: i for i, v in enumerate(ids)}
for want in (2200, 12556, 11029, 110635, 9582):
    i = idx.get(want)
    if i is None: print("  id %d: НЕТ" % want); continue
    s = bytes(((b-100) & 0xFF) for b in nd[4+256*i:4+256*i+256]).split(b'\0')[0]
    print("  id %-6d -> %s" % (want, s.decode('cp1251', 'replace')[:60]))

print("=== loose ===")
for rel, stride, lb, keyspec, fields in LOOSE_SPECS:
    base = os.path.splitext(os.path.basename(rel))[0]
    np_ = os.path.join(OUT, "Data", rel.replace("/", os.sep))
    op = os.path.join(ORIG_CLIENT, "Data", rel.replace("/", os.sep))
    if not os.path.isfile(np_): print("  - %s: нет выхода" % base); continue
    od = open(op, "rb").read(); nd = open(np_, "rb").read()
    oh, orecs, ook = parse_loose(od, stride, lb)
    if not orecs: print("  OK %-28s (пустая)" % base); continue
    zones = [(off, stride) for _, off in fields]   # конец поля неизвестен -> до конца записи, дифф-чек мягкий
    check_zones(od, nd, stride, len(oh), len(orecs), zones, "loose/"+base)
    _, nrecs, _ = parse_loose(nd, stride, lb)
    f0 = fields[0]
    s = read_cstr(nrecs[0], f0[1], stride - f0[1]).decode('cp1251', 'replace')
    print("  OK %-28s пример: %s" % (base, s[:44]))

print("=== talk (выборочно) ===")
tdir = os.path.join(OUT, "Data", "NPC", "Episode", "Talk")
names = sorted(os.listdir(tdir))[:3]
for fn in names:
    d = open(os.path.join(tdir, fn), "rb").read()
    _, recs, _ = parse_loose(d, 208, False)
    if recs:
        print("  %s: %s" % (fn, read_cstr(recs[0], 5, 203).decode('cp1251', 'replace')[:60]))

print()
if fails:
    print("!!! ПРОВАЛЫ (%d):" % len(fails))
    for f in fails: print("  ", f)
    sys.exit(1)
print("ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ: диффы только в текст-зонах, MD5 валиден, структура 1:1.")
