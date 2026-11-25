# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
merge_jui.py — перенос ТЕКСТА интерфейса из juidata Фантазиума в juidata MINE.

Почему нельзя просто подменить папку: GameMain.JUI Фантазиума (2014) содержит МЕНЬШЕ окон,
чем клиент MINE (18924 контрола против 17xxx; 40 скин-контролов есть только у MINE).
EXE обращается к контролам по ID/имени — отсутствующие окна дают ошибки. Поэтому здесь
переносится только ТЕКСТ совпавших контролов, структура окон MINE сохраняется 1:1.

Сопоставление: путь в дереве контролов (иерархия Name через CHS/CHE) + запасной ключ
Name+ID. Переносимые ключи: CaptionText, TextContent, ToopTip (JUI) и CaptionText (JUS).
Языковой фильтр общий с merge_translate.py: корейское/пустое не затирает готовый текст.
"""
import os, io, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jui_codec as J
from merge_translate import lang, _RANK

ORIG = r"@@ASTORY_DEV_ROOT@@\Donors\AsdaChroniclesInterlude_MINE\juidata"
FANTA = r"@@ASTORY_DEV_ROOT@@\Donors\Fantazium\juidata"
OUT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\out_client\juidata"
REPORTS = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Tools\Translate\reports\jui"
os.makedirs(OUT, exist_ok=True)
os.makedirs(REPORTS, exist_ok=True)

TEXT_KEYS = ("CaptionText", "TextContent", "ToopTip")

_KV = re.compile(rb'^(\s*)([A-Za-z_]+)\s*=\s*"(.*)"\s*$')

def parse(plain):
    """-> список записей {key, line_idx_by_field}: ключ = путь имён, значения — номера строк."""
    lines = plain.split(b"\r\n")
    stack = []          # имена родителей
    last_name = None    # имя последнего разобранного контрола (кандидат в родители при CHS)
    cur = None
    ctrls = []
    seen = {}
    for i, ln in enumerate(lines):
        m = _KV.match(ln)
        if ln.strip() == b"[Ctrl]":
            if cur: ctrls.append(cur)
            cur = {"fields": {}, "name": None, "id": None, "path": list(stack)}
            continue
        if not m:
            continue
        indent, key, val = m.group(1), m.group(2).decode(), m.group(3)
        if key == "CHS":
            if cur: ctrls.append(cur); last_name = cur["name"]; cur = None
            stack.append(last_name or "?")
            continue
        if key == "CHE":
            if cur: ctrls.append(cur); last_name = cur["name"]; cur = None
            if stack: stack.pop()
            continue
        if key == "Name":
            if cur is None:                      # вложенный контрол начинается прямо с Name
                cur = {"fields": {}, "name": None, "id": None, "path": list(stack)}
            elif cur["name"] is not None:        # начался следующий контрол без [Ctrl]
                ctrls.append(cur); last_name = cur["name"]
                cur = {"fields": {}, "name": None, "id": None, "path": list(stack)}
            cur["name"] = val.decode("cp1251", "replace")
            continue
        if cur is None:
            continue
        if key == "ID":
            cur["id"] = val.decode("cp1251", "replace")
        if key in TEXT_KEYS:
            cur["fields"][key] = (i, val)
    if cur: ctrls.append(cur)
    # ключи с разрешением дубликатов
    for c in ctrls:
        base = "/".join(c["path"] + [c["name"] or "?"]) + "|" + (c["id"] or "")
        n = seen.get(base, 0); seen[base] = n + 1
        c["key"] = base if n == 0 else "%s#%d" % (base, n)
    return lines, ctrls

def merge_file(name, out_name, summary):
    op = os.path.join(ORIG, name); fp = os.path.join(FANTA, name)
    if not os.path.isfile(op) or not os.path.isfile(fp):
        summary.append((name, "НЕТ ФАЙЛА", 0, 0, 0)); return
    raw_o = open(op, "rb").read(); raw_f = open(fp, "rb").read()
    po = J.decode(raw_o); pf = J.decode(raw_f)
    lines_o, ctrls_o = parse(po)
    _, ctrls_f = parse(pf)
    fmap = {c["key"]: c for c in ctrls_f}
    rep = []
    replaced = nopair = worse = 0
    touched_ctrl = 0
    for c in ctrls_o:
        f = fmap.get(c["key"])
        if f is None:
            if any(v[1].strip() for v in c["fields"].values()):
                nopair += 1
                rep.append("%s\t[НЕТ ПАРЫ]\t%s" % (c["key"], " | ".join(
                    "%s=%s" % (k, v[1].decode("cp1251", "replace")) for k, v in c["fields"].items() if v[1].strip())))
            continue
        did = False
        for k, (li, ov) in c["fields"].items():
            fv = f["fields"].get(k, (None, b""))[1]
            if not fv.strip():
                continue
            if _RANK[lang(fv)] < _RANK[lang(ov)]:
                worse += 1
                rep.append("%s\t[FANTA-ХУЖЕ %s: %s<-%s]\t%s\t=>\t%s" % (
                    c["key"], k, lang(ov), lang(fv),
                    ov.decode("cp1251", "replace"), fv.decode("cp1251", "replace")))
                continue
            if fv == ov:
                continue
            m = _KV.match(lines_o[li])
            lines_o[li] = b'%s%s = "%s"' % (m.group(1), k.encode(), fv)
            replaced += 1; did = True
        if did: touched_ctrl += 1
    out_plain = b"\r\n".join(lines_o)
    assert len(out_plain.split(b"\r\n")) == len(lines_o)
    outp = os.path.join(OUT, out_name)
    open(outp, "wb").write(J.encode(out_plain, raw_o[0]))
    # раунд-трип: файл должен декодироваться обратно 1:1
    assert J.decode(open(outp, "rb").read()) == out_plain, "раунд-трип " + out_name
    with io.open(os.path.join(REPORTS, os.path.splitext(name)[0] + "_missing.txt"), "w", encoding="utf-8-sig") as fo:
        fo.write("# %s: контролов=%d, полей заменено=%d, контролов затронуто=%d, без пары=%d, fanta-хуже=%d\n"
                 % (name, len(ctrls_o), replaced, touched_ctrl, nopair, worse))
        fo.write("\n".join(rep))
    summary.append((name, "OK", len(ctrls_o), replaced, nopair))

def main():
    summary = []
    for name in ("GameMain.JUI", "Login.JUI", "Temp.JUI", "commonwin.JUI", "JUISKIN.JUS"):
        merge_file(name, name, summary)
    print("%-16s %-10s %9s %9s %9s" % ("файл", "статус", "контролов", "заменено", "без пары"))
    for r in summary:
        print("%-16s %-10s %9s %9s %9s" % r)

if __name__ == "__main__":
    main()
