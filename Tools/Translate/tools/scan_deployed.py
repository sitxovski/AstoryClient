# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
scan_deployed.py — отчёт о НЕПЕРЕВЕДЁННОМ тексте по ФАКТИЧЕСКИМ файлам клиента
(не по промежуточным результатам слияния).

Читает так, как читает сам клиент, с учётом применённого патча длины строки:
  * поле зашифровано и таблица ПРОПАТЧЕНА -> дешифруем поле целиком, строка до первого 0x00
    (именно так теперь работает загрузчик, поэтому «я» внутри строки видна);
  * поле зашифровано, патча нет           -> strlen до 0x00, затем дешифровка (как раньше);
  * поле не зашифровано                   -> строка до 0x00 как есть.

Классификация: rus / lat (английский) / kor (непереведённый корейский) / neutral (цифры,
знаки, имена ресурсов) / empty. В отчёты идут ТОЛЬКО lat и kor.

Вывод: @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\untranslated_deployed\
  _СВОДКА.txt        — счётчики по всем источникам
  <таблица>.txt      — построчно: ключ, поле, язык, текст
"""
import os, io, sys, struct, zipfile, glob

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\ItemDB")
import decode_item_db as D
from merge_translate import dec_text, BBZ_SPECS, LOOSE_SPECS, parse_loose, loose_key
# ВАЖНО: детектор берём из langdet, а НЕ из merge_translate. Тот проверял cp949 первым и
# помечал русский текст как корейский («Золото» → валидные слоги cp949), из-за чего отчёт
# показывал переведённые строки как непереведённые.
from langdet import lang

CLIENT = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE"
OUTDIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\untranslated_deployed"
os.makedirs(OUTDIR, exist_ok=True)
# Каталог ОЧИЩАЕТСЯ перед каждым прогоном: иначе таблица, где непереведённых стало ноль,
# сохраняла бы старый отчёт с прошлого запуска и выглядела бы непереведённой.
for _f in os.listdir(OUTDIR):
    _p = os.path.join(OUTDIR, _f)
    if os.path.isfile(_p) and _f.endswith(".txt"):
        os.remove(_p)

# поля, для которых применён патч «длина = весь размер поля» (см. patch_name_len.py)
PATCHED = {
    ("item_list.bin", "ItemName"), ("item_tooltip.bin", "Description"),
    ("sowell_list.bin", "Name"), ("sowell_tooltip.bin", "Text"),
    ("special_itemlist.bin", "Name"),
    ("specialitem_tooltiplist.bin", "Name"), ("specialitem_tooltiplist.bin", "Tooltip"),
    ("title_list.bin", "Name"), ("title_list.bin", "Desc"),
}

def read_field(body, off, cap, encrypted, patched):
    raw = bytes(body[off:off + cap])
    if not encrypted:
        z = raw.find(b"\x00")
        return raw[:z] if z >= 0 else raw
    if patched:
        plain = bytes(((b - 1) & 0xFF) for b in raw)
        z = plain.find(b"\x00")
        return plain[:z] if z >= 0 else plain
    z = raw.find(b"\x00")
    part = raw[:z] if z >= 0 else raw
    return bytes(((b - 1) & 0xFF) for b in part)

def strip_marker(b):
    return b[:-1] if b.endswith(b"~") else b

def dump(name, header, lines):
    if not lines:
        return 0
    with io.open(os.path.join(OUTDIR, name), "w", encoding="utf-8-sig") as f:
        f.write("# %s\n# ключ\tполе\tязык\tтекст\n" % header)
        f.write("\n".join(lines))
    return len(lines)

def key_of(body, spec):
    km = spec["keymode"]
    if km == "str":
        raw = bytes(body[spec["keyoff"]:spec["keyoff"] + spec.get("keycap", 32)])
        z = raw.find(b"\x00")
        return dec_text(raw[:z] if z >= 0 else raw)
    t = spec.get("keytyp", "i32"); o = spec.get("keyoff", 0)
    fmt = {"i32": "<i", "u32": "<I", "u16": "<H", "i16": "<h"}[t]
    return struct.unpack_from(fmt, body, o)[0]

def scan_bbz(summary):
    p = os.path.join(CLIENT, "Data", "Character", "Bin.bbz")
    z = zipfile.ZipFile(p)
    names = {n.lower(): n for n in z.namelist()}
    for entry, spec in BBZ_SPECS.items():
        reg = D.REGISTRY.get(entry)
        real = names.get(entry.lower())
        if reg is None or real is None:
            continue
        d = z.read(real)
        stride, md5 = reg["stride"], reg.get("md5", 0)
        noheader = reg.get("noheader", False)
        base = 0 if noheader else 20
        cnt = (len(d) - base) // stride if noheader else struct.unpack_from("<i", d, 4)[0]
        lines = []
        stat = {"rus": 0, "lat": 0, "kor": 0, "neutral": 0, "empty": 0}
        for i in range(cnt):
            o = base + i * stride
            if o + stride > len(d):
                break
            body = d[o + md5:o + stride]
            k = i if spec["keymode"] == "pos" else key_of(body, spec)
            for (fname, off, cap, enc) in spec["fields"]:
                t = strip_marker(read_field(body, off, cap, enc, (entry, fname) in PATCHED))
                lg = lang(t)
                stat[lg] += 1
                if lg in ("lat", "kor"):
                    lines.append("%s\t%s\t%s\t%s" % (k, fname, lg, dec_text(t)))
        n = dump(os.path.splitext(entry)[0].replace(" ", "_") + ".txt",
                 "%s (Bin.bbz): непереведённых полей %d" % (entry, len(lines)), lines)
        summary.append(("Bin.bbz/" + entry, cnt, stat["rus"], stat["lat"], stat["kor"], n))
    z.close()

def scan_loose(summary):
    for rel, stride, lb, keyspec, fields in LOOSE_SPECS:
        p = os.path.join(CLIENT, "Data", rel.replace("/", os.sep))
        if not os.path.isfile(p):
            continue
        d = open(p, "rb").read()
        _, recs, ok = parse_loose(d, stride, lb)
        if not recs:
            continue
        lines = []
        stat = {"rus": 0, "lat": 0, "kor": 0, "neutral": 0, "empty": 0}
        for rec in recs:
            k = loose_key(rec, keyspec)
            for nm, off in fields:
                raw = bytes(rec[off:stride])
                z = raw.find(b"\x00")
                t = raw[:z] if z >= 0 else raw
                lg = lang(t)
                stat[lg] += 1
                if lg in ("lat", "kor"):
                    lines.append("%s\t%s\t%s\t%s" % (k, nm, lg, dec_text(t)))
        base = os.path.splitext(os.path.basename(rel))[0]
        n = dump(base + ".txt", "%s: непереведённых полей %d" % (rel, len(lines)), lines)
        summary.append(("Data/" + rel, len(recs), stat["rus"], stat["lat"], stat["kor"], n))

def scan_error_list(summary):
    p = os.path.join(CLIENT, "system", "error list.bin")
    d = open(p, "rb").read()
    cnt = struct.unpack_from("<I", d, 0)[0]
    ids = struct.unpack_from("<%di" % cnt, d, 4 + 256 * cnt)
    lines = []
    stat = {"rus": 0, "lat": 0, "kor": 0, "neutral": 0, "empty": 0}
    for i in range(cnt):
        rec = bytes(((b - 100) & 0xFF) for b in d[4 + 256 * i:4 + 256 * (i + 1)])
        z = rec.find(b"\x00")
        t = rec[:z] if z >= 0 else rec
        lg = lang(t)
        stat[lg] += 1
        if lg in ("lat", "kor"):
            lines.append("%s\t%s\t%s\t%s" % (ids[i], "String", lg, dec_text(t)))
    n = dump("error_list.txt", "system/error list.bin: непереведённых строк %d" % len(lines), lines)
    summary.append(("system/error list.bin", cnt, stat["rus"], stat["lat"], stat["kor"], n))

def scan_talk(summary):
    tdir = os.path.join(CLIENT, "Data", "NPC", "Episode", "Talk")
    if not os.path.isdir(tdir):
        return
    lines = []
    stat = {"rus": 0, "lat": 0, "kor": 0, "neutral": 0, "empty": 0}
    # ВНИМАНИЕ: на Windows файловая система регистронезависима, поэтому glob("*.BIN") и
    # glob("*.bin") возвращают ОДНИ И ТЕ ЖЕ файлы — из-за этого реплики считались дважды
    # (в отчёте было 406 вместо 203). Дедупликация по нормализованному пути.
    files = sorted({os.path.normcase(p) for p in
                    glob.glob(os.path.join(tdir, "*.BIN")) + glob.glob(os.path.join(tdir, "*.bin"))})
    for path in files:
        d = open(path, "rb").read()
        if len(d) < 20:
            continue
        cnt = struct.unpack_from("<I", d, 4)[0]
        npc = os.path.splitext(os.path.basename(path))[0]
        for i in range(cnt):
            o = 20 + i * 208
            if o + 208 > len(d):
                break
            raw = bytes(d[o + 5:o + 208])
            z = raw.find(b"\x00")
            t = raw[:z] if z >= 0 else raw
            lg = lang(t)
            stat[lg] += 1
            if lg in ("lat", "kor"):
                lines.append("%s\t%s\t%s\t%s" % (npc, "line%d" % i, lg, dec_text(t)))
    n = dump("Talk_dialogues.txt", "Talk/*.BIN (%d файлов): непереведённых реплик %d" % (len(files), len(lines)), lines)
    summary.append(("Data/NPC/Episode/Talk/*.BIN", sum(stat.values()), stat["rus"], stat["lat"], stat["kor"], n))

def main():
    summary = []
    scan_bbz(summary)
    scan_loose(summary)
    scan_error_list(summary)
    scan_talk(summary)
    hdr = "%-46s %8s %8s %8s %8s" % ("источник", "записей", "рус", "англ", "кор")
    rows = []
    for src, cnt, rus, lat, kor, n in summary:
        rows.append("%-46s %8d %8d %8d %8d" % (src, cnt, rus, lat, kor))
    tot_lat = sum(s[3] for s in summary); tot_kor = sum(s[4] for s in summary)
    tot_rus = sum(s[2] for s in summary)
    with io.open(os.path.join(OUTDIR, "_СВОДКА.txt"), "w", encoding="utf-8-sig") as f:
        f.write("# Непереведённый текст в задеплоенном клиенте\n")
        f.write("# Считано по фактическим файлам %s\n#\n" % CLIENT)
        f.write(hdr + "\n" + "\n".join(rows))
        f.write("\n\nИТОГО: русских полей %d, английских %d, корейских %d\n" % (tot_rus, tot_lat, tot_kor))
    print(hdr)
    print("\n".join(rows))
    print("\nИТОГО: рус %d | англ %d | кор %d" % (tot_rus, tot_lat, tot_kor))
    print("отчёты: %s" % OUTDIR)

if __name__ == "__main__":
    main()
