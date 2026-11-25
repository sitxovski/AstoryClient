# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
"""asda_split_check.py — проверка, что разрезание .cpp было ДОСЛОВНЫМ переносом (рефакторинг R2, 10.11.2026).

Вход: исходный файл(ы) «до» (из бэкапа) и набор файлов «после» (части X_<Часть>.cpp + общий X_Internal.h).
Разбор верхнего уровня C++ с учётом строк, комментариев, сырых литералов, #if/#endif и пространств имён:
определения функций/методов, типы, file-static/анонимные объекты, вызовы макросов, #define, #include.

Сверка:
  * каждое определение «до» ровно один раз присутствует «после» с тем же текстом (нормализуются только
    пробелы на концах строк, переводы строк, общий отступ и спецификаторы связывания static/inline/[[maybe_unused]] —
    их смена выводится отдельным списком LINKAGE);
  * условие препроцессора (#if-стек) и пространство имён у определения не поменялись;
  * ничего лишнего не появилось, кроме #include, объявлений (прототипы/extern/вперёд) и комментариев;
  * порядок объектов со статической инициализацией внутри одного файла «после» сохранён; объекты регистрации
    (консольные переменные/команды) выводятся отдельным списком «до → после» — это риск порядка инициализации;
  * в общем заголовке нет изменяемого file-static состояния и static-функций с локальными static
    (каждая единица трансляции получила бы свою копию — поведение поменялось бы).

Выход: отчёт (stdout и/или --report FILE) и код возврата 0 = чисто, 1 = расхождения, 2 = ошибка разбора/вызова.
Режимы: --selftest (синтетический пример), --dump FILE (список определений верхнего уровня с диапазонами строк).

Примеры:
  python asda_split_check.py --before _backup/.../AsdaX.cpp --after Private/AsdaX.cpp Private/AsdaX_Net.cpp Private/AsdaX_Internal.h
  python asda_split_check.py --dump Private/AsdaNetSubsystem.cpp --json out.json
  python asda_split_check.py --selftest
"""
import argparse
import difflib
import json
import os
import re
import sys
from collections import defaultdict

# ─────────────────────────────── константы ───────────────────────────────

# Спецификаторы связывания/атрибуты, смена которых при переносе в общий заголовок допустима (выводится как LINKAGE).
STORAGE_TOKENS = {"static", "inline", "FORCEINLINE", "FORCENOINLINE"}
# Ключевые слова, начинающие определение типа.
TYPE_KEYWORDS = {"class", "struct", "union", "enum"}
# Токены, которые могут идти после ')' вызова макроса, если это НЕ вызов макроса без ';', а продолжение объявления.
CONT_AFTER_PAREN = {"{", ";", ":", "const", "override", "final", "noexcept", "->", "=", "&", "&&", "throw",
                    "requires", "try", "volatile", "mutable", "(", "[", ",", ")", "::", "<", ">", "."}
# Спецификаторы, которые могут стоять одни в строке перед объявлением (не считать «голым» макросом).
SPECIFIER_MACROS = {"FORCEINLINE", "FORCENOINLINE", "ASDAREBORN_API", "UE_NODISCARD", "CONSTEXPR", "RESTRICT",
                    "UE_DEPRECATED", "PRAGMA_DISABLE_OPTIMIZATION_ACTUAL"}
# Признак типа «объект регистрации» (консольные переменные/команды и т. п.) — риск порядка инициализации.
REG_RE = re.compile(r"ASDA_DEV_CVAR|ASDA_DEV_CMD|AutoConsole|ConsoleVariable|ConsoleCommand|ConsoleObject|TAsdaConstCVar|AsdaDevCVar|"
                    r"FAutoRegister|FDelayedAutoRegister|IMPLEMENT_|FSelfRegistering|AsdaSelfTest|Register")
# Пары макросов категорий журнала, которые разрезание законно превращает: STATIC → (DECLARE_EXTERN в заголовке + DEFINE).
LOG_STATIC = "DEFINE_LOG_CATEGORY_STATIC"
# Расширения заголовков (для проверок «состояние в общем заголовке»).
HEADER_EXT = (".h", ".hpp", ".inl", ".hh")
# Однословные/двусловные знаки пунктуации, которые лексер собирает в один токен.
PUNCT2 = ("::", "->", "&&", "||", "==", "!=", "<=", ">=", "<<", "+=", "-=", "*=", "/=", "|=", "&=", "^=", "%=",
          "++", "--", "##", "...", ".*", "->*")


# ─────────────────────────────── лексер ───────────────────────────────

class Tok:
    """Токен исходника: вид, текст, смещения [start,end) в нормализованном тексте, номер строки начала."""
    __slots__ = ("kind", "text", "start", "end", "line", "nl_before")

    def __init__(self, kind, text, start, end, line, nl_before):
        self.kind = kind          # id | num | str | chr | punct | pp | comment
        self.text = text          # текст токена (для pp/comment — вся директива/комментарий)
        self.start = start        # смещение начала
        self.end = end            # смещение конца (не включая)
        self.line = line          # номер строки начала (с 1)
        self.nl_before = nl_before  # перед токеном был перевод строки (токен первый в своей строке)

    def __repr__(self):
        return "Tok(%s,%r,L%d)" % (self.kind, self.text[:30], self.line)


class LexError(Exception):
    """Ошибка лексического разбора (незакрытая строка/комментарий/сырой литерал)."""


# Регулярка префикса строкового литерала (L, u, U, u8) с необязательным R.
_STR_PREFIX = re.compile(r'(?:u8|u|U|L)?R?"')
# Регулярка идентификатора.
_ID_RE = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*")
# Регулярка числа (с разделителями ' и экспонентой со знаком).
_NUM_RE = re.compile(r"\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.']|)*")


def normalize_text(raw):
    """Снять BOM и привести переводы строк к \\n (позиции и номера строк считаются по результату)."""
    if raw.startswith("﻿"):
        raw = raw[1:]
    return raw.replace("\r\n", "\n").replace("\r", "\n")


def lex(text):
    """Разбить нормализованный текст на токены (комментарии и директивы — отдельными токенами целиком)."""
    toks = []          # итоговый список токенов
    i = 0              # текущая позиция
    n = len(text)      # длина текста
    line = 1           # текущая строка
    at_line_start = True  # с начала строки были только пробелы (для распознавания '#')
    nl_pending = True  # между прошлым токеном и текущим был перевод строки
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            at_line_start = True
            nl_pending = True
            continue
        if c in " \t\f\v":
            i += 1
            continue
        if c == "\\" and i + 1 < n and text[i + 1] == "\n":   # склейка строк вне директивы
            i += 2
            line += 1
            continue
        start = i
        sline = line
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            toks.append(Tok("comment", text[i:j], i, j, sline, nl_pending))
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            if j < 0:
                raise LexError("незакрытый /* в строке %d" % sline)
            j += 2
            line += text.count("\n", i, j)
            toks.append(Tok("comment", text[i:j], i, j, sline, nl_pending))
            i = j
            continue
        if c == "#" and at_line_start:
            j = i
            while j < n:                      # директива до перевода строки без '\' (комментарии внутри учитываются)
                ch = text[j]
                if ch == "\\" and j + 1 < n and text[j + 1] == "\n":
                    j += 2
                    continue
                if ch == "/" and j + 1 < n and text[j + 1] == "*":
                    k = text.find("*/", j + 2)
                    if k < 0:
                        raise LexError("незакрытый /* в директиве, строка %d" % sline)
                    j = k + 2
                    continue
                if ch == "/" and j + 1 < n and text[j + 1] == "/":
                    k = text.find("\n", j)
                    j = n if k < 0 else k
                    break
                if ch == "\n":
                    break
                j += 1
            line += text.count("\n", i, j)
            toks.append(Tok("pp", text[i:j], i, j, sline, True))
            i = j
            at_line_start = False
            nl_pending = False
            continue
        at_line_start = False
        m = _STR_PREFIX.match(text, i)
        if m:
            pre = m.group(0)
            if "R" in pre:                    # сырой литерал R"delim( ... )delim"
                k = text.find("(", m.end())
                if k < 0:
                    raise LexError("битый сырой литерал в строке %d" % sline)
                delim = text[m.end():k]
                close = ")" + delim + '"'
                e = text.find(close, k + 1)
                if e < 0:
                    raise LexError("незакрытый сырой литерал в строке %d" % sline)
                j = e + len(close)
            else:
                j = m.end()
                while j < n and text[j] != '"':
                    if text[j] == "\\":
                        j += 1
                    elif text[j] == "\n":
                        raise LexError("перевод строки внутри строки, строка %d" % sline)
                    j += 1
                j += 1
            line += text.count("\n", i, j)
            toks.append(Tok("str", text[i:j], i, j, sline, nl_pending))
            nl_pending = False
            i = j
            continue
        if c == "'" or (c in "LuU" and text[i + 1:i + 2] == "'") or (text.startswith("u8'", i)):
            j = text.find("'", i) + 1
            while j < n and text[j] != "'":
                if text[j] == "\\":
                    j += 1
                elif text[j] == "\n":
                    raise LexError("перевод строки внутри символа, строка %d" % sline)
                j += 1
            j += 1
            toks.append(Tok("chr", text[i:j], i, j, sline, nl_pending))
            nl_pending = False
            i = j
            continue
        if c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            m = _NUM_RE.match(text, i)
            j = m.end()
            toks.append(Tok("num", text[i:j], i, j, sline, nl_pending))
            nl_pending = False
            i = j
            continue
        m = _ID_RE.match(text, i)
        if m:
            j = m.end()
            toks.append(Tok("id", text[i:j], i, j, sline, nl_pending))
            nl_pending = False
            i = j
            continue
        p = None
        for cand in ("->*", "...", "<<=", ">>="):
            if text.startswith(cand, i):
                p = cand
                break
        if p is None:
            two = text[i:i + 2]
            p = two if two in PUNCT2 else c
        toks.append(Tok("punct", p, i, i + len(p), sline, nl_pending))
        nl_pending = False
        i += len(p)
    return toks


# ─────────────────────────────── разбор верхнего уровня ───────────────────────────────

class Item:
    """Элемент верхнего уровня: определение функции/типа/объекта, макрос, директива или объявление."""

    def __init__(self, kind, file, start_tok, end_tok, cond, ns):
        self.kind = kind          # func | type | object | decl | alias | assert | macro_call | define | include | pragma | undef
        self.file = file          # имя файла
        self.toks = []            # токены (без комментариев) от начала до конца
        self.start = start_tok.start  # смещение начала в тексте
        self.end = end_tok.end    # смещение конца
        self.line_start = start_tok.line  # первая строка
        self.line_end = 0         # последняя строка (заполняется парсером)
        self.cond = cond          # условие препроцессора (строка, '' = безусловно)
        self.ns = ns              # пространство имён ('' = глобальное, '(anon)' — анонимное)
        self.key = ""             # ключ сверки
        self.name = ""            # короткое имя (для отчёта)
        self.storage = ()         # снятые спецификаторы связывания
        self.cmp_text = ""        # нормализованный текст для побайтной сверки
        self.doc_start = self.start  # начало вместе с прилегающим комментарием сверху (для размеров в картах)
        self.doc_line = self.line_start  # строка начала вместе с комментарием
        self.index = -1           # порядковый номер в файле
        self.local_static = False  # в теле функции есть локальная static
        self.mutable = False      # объект без const/constexpr (изменяемое состояние)
        self.registration = False  # объект регистрации (консольная переменная/команда и т. п.)

    def size(self):
        """Размер в байтах (UTF-8) вместе с комментарием сверху."""
        return self.size_bytes

    def to_dict(self):
        """Представление для JSON."""
        return {"kind": self.kind, "name": self.name, "key": self.key, "cond": self.cond, "ns": self.ns,
                "line_start": self.line_start, "line_end": self.line_end, "doc_line": self.doc_line,
                "bytes": self.size_bytes, "storage": list(self.storage), "registration": self.registration,
                "mutable": self.mutable, "local_static": self.local_static}


class ParseError(Exception):
    """Ошибка разбора верхнего уровня (несбалансированные скобки и т. п.)."""


def _norm_join(toks):
    """Склеить токены в нормализованную строку (один пробел между токенами, без пробелов вокруг '::')."""
    out = []
    for t in toks:
        if t.kind in ("comment", "pp"):
            continue
        out.append(t.text)
    s = " ".join(out)
    return s.replace(" :: ", "::").replace(":: ", "::").replace(" ::", "::")


def _dedent_rstrip(s):
    """Нормализация текста для сверки: rstrip каждой строки, снятие общего отступа, обрезка пустых строк по краям."""
    lines = [ln.rstrip() for ln in s.split("\n")]
    while lines and not lines[0]:
        lines.pop(0)
    while lines and not lines[-1]:
        lines.pop()
    ind = None
    for ln in lines[1:] if len(lines) > 1 else lines:
        if not ln:
            continue
        lead = len(ln) - len(ln.lstrip(" \t"))
        ind = lead if ind is None else min(ind, lead)
    ind = ind or 0
    first = lines[0].lstrip(" \t") if lines else ""
    rest = [ln[ind:] if len(ln) >= ind else ln.lstrip(" \t") for ln in lines[1:]]
    return "\n".join([first] + rest)


def _cond_of(stack):
    """Строковое представление стека условий препроцессора."""
    return " && ".join(c for c in stack if c)


def _pp_parse(text):
    """Разобрать директиву: (имя директивы, остаток) без комментариев и склеек."""
    t = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    t = re.sub(r"//.*", "", t)
    t = t.replace("\\\n", " ")
    m = re.match(r"\s*#\s*([A-Za-z_]+)?\s*(.*)", t, re.S)
    if not m:
        return "", ""
    return (m.group(1) or ""), " ".join(m.group(2).split())


def _is_macro_name(s):
    """Похоже на имя макроса: ВСЕ_ЗАГЛАВНЫЕ (с цифрами/подчёркиваниями), длиной ≥ 3."""
    return len(s) >= 3 and re.fullmatch(r"[A-Z][A-Z0-9_]*", s) is not None and any(ch.isalpha() for ch in s)


class FileModel:
    """Разобранный файл: элементы верхнего уровня и комментарии верхнего уровня."""

    def __init__(self, name, raw, strip_include_guard=False):
        self.name = name                          # имя файла (для отчёта)
        self.text = normalize_text(raw)           # нормализованный текст
        self.is_header = name.lower().endswith(HEADER_EXT)  # это заголовок
        self.items = []                           # элементы верхнего уровня
        self.top_comments = []                    # комментарии верхнего уровня (нормализованный текст, строка)
        self.errors = []                          # ошибки разбора
        self._guard = None                        # имя include-guard (#ifndef X / #define X) — условие не учитывается
        self._parse(strip_include_guard or self.is_header)

    # ── вспомогательное ──
    def _line_of(self, off):
        """Номер строки смещения."""
        return self.text.count("\n", 0, off) + 1

    def _parse(self, strip_guard):
        """Разобрать файл в элементы верхнего уровня."""
        toks = lex(self.text)
        self.all_toks = toks
        code = [t for t in toks if t.kind != "comment"]   # токены без комментариев
        comments = [t for t in toks if t.kind == "comment"]
        self.cond_stack = []                              # стек условий: список строк текущих ветвей
        self.cond_hist = []                               # стек: список предыдущих ветвей уровня (для #else/#elif)
        scope = []                                        # стек пространств имён: (вид, имя)
        if strip_guard:
            pps = [t for t in code[:2] if t.kind == "pp"]
            if len(pps) == 2:
                d0, a0 = _pp_parse(pps[0].text)
                d1, a1 = _pp_parse(pps[1].text)
                if d0 == "ifndef" and d1 == "define" and a1.split()[:1] == [a0]:
                    self._guard = a0
        i = 0
        n = len(code)
        spans = []                                        # занятые элементами диапазоны (для комментариев)
        while i < n:
            t = code[i]
            if t.kind == "pp":
                self._handle_pp(t, scope)
                i += 1
                continue
            if t.text == "}" and t.kind == "punct":
                if not scope:
                    self.errors.append("лишняя '}' на строке %d" % t.line)
                else:
                    scope.pop()
                i += 1
                continue
            if t.text == ";" and t.kind == "punct":
                i += 1
                continue
            j = i
            if t.text == "inline" and j + 1 < n and code[j + 1].text == "namespace":
                j += 1
            if code[j].text == "namespace" and code[j].kind == "id":
                k = j + 1
                name_t = []
                while k < n and code[k].text not in ("{", "=", ";"):
                    name_t.append(code[k].text)
                    k += 1
                if k < n and code[k].text == "{":
                    scope.append(("ns", "".join(name_t) or "(anon)"))
                    i = k + 1
                    continue
                # псевдоним пространства имён: namespace A = B;
                it = self._mk_simple("alias", code, i, self._find_semicolon(code, k), scope)
                i = self._after(it, code)
                continue
            if t.text == "extern" and i + 2 < n and code[i + 1].kind == "str" and code[i + 2].text == "{":
                scope.append(("extern", code[i + 1].text))
                i += 3
                continue
            if self._bare_macro(code, i):
                it = Item("macro_call", self.name, t, t, self._cond(), self._ns(scope))
                it.toks = [t]
                it.line_end = t.line
                self._finish(it, scope)
                i += 1
                continue
            i = self._statement(code, i, scope)
        if scope:
            self.errors.append("не закрыто пространств имён/блоков: %d (%s)" % (len(scope), scope))
        if self.cond_stack:
            self.errors.append("не закрыто #if: %d" % len(self.cond_stack))
        # хвостовой комментарий в строке конца определения («…;   // подпись») — часть определения: иначе при переносе он
        #   мог бы «уехать» к соседу (смысл подписи меняется), а сверка этого не увидела бы (R2, урок пилота 10.11)
        cstart = {}
        self._trailing = set()                # начала хвостовых комментариев (принадлежат определению слева)
        for c in comments:
            cstart.setdefault(c.start, c)
        import bisect
        cstarts = sorted(cstart)
        for it in self.items:
            if it.kind in ("include", "define", "undef", "pragma"):
                continue
            k = bisect.bisect_left(cstarts, it.end)
            if k < len(cstarts):
                c = cstart[cstarts[k]]
                gap = self.text[it.end:c.start]
                if "\n" not in gap and gap.strip(" \t") == "" and c.text.startswith("//"):
                    self._trailing.add(c.start)
                    it.end = c.end
                    it.cmp_text = _dedent_rstrip(it.cmp_text + gap + c.text)
        for it in self.items:
            spans.append((it.start, it.end))
        spans.sort()
        # комментарии верхнего уровня = вне любых элементов
        si = 0
        for c in comments:
            while si < len(spans) and spans[si][1] <= c.start:
                si += 1
            inside = si < len(spans) and spans[si][0] <= c.start < spans[si][1]
            if not inside:
                self.top_comments.append((_dedent_rstrip(c.text), c.line))
        self._attach_docs(comments)
        for idx, it in enumerate(self.items):
            it.index = idx
            it.size_bytes = len(self.text[it.doc_start:it.end].encode("utf-8"))

    def _attach_docs(self, comments):
        """Привязать к элементам прилегающие сверху комментарии (только для размеров/диапазонов карт)."""
        cstarts = {c.end: c for c in comments}
        by_end = sorted(comments, key=lambda c: c.end)
        ends = [c.end for c in by_end]
        import bisect
        for it in self.items:
            pos = it.start
            while True:
                # ближайший комментарий, кончающийся до pos и отделённый только пробелами/одним переводом строки
                k = bisect.bisect_right(ends, pos) - 1
                if k < 0:
                    break
                c = by_end[k]
                if c.start in self._trailing:
                    break                     # хвост предыдущего определения — не шапка этого
                gap = self.text[c.end:pos]
                if gap.strip(" \t") not in ("", "\n") or gap.count("\n") > 1:
                    break
                pos = c.start
                # начало строки комментария
            ls = self.text.rfind("\n", 0, pos) + 1
            if self.text[ls:pos].strip() == "":
                pos = ls
            it.doc_start = pos
            it.doc_line = self._line_of(pos)
        del cstarts

    def _cond(self):
        """Текущее условие препроцессора."""
        return _cond_of(self.cond_stack)

    def _ns(self, scope):
        """Текущее пространство имён."""
        return "::".join(nm if kind == "ns" else "extern" + nm for kind, nm in scope)

    def _handle_pp(self, t, scope):
        """Обработать директиву верхнего уровня: условия, #include, #define, #undef, #pragma."""
        d, rest = _pp_parse(t.text)
        if d in ("if", "ifdef", "ifndef"):
            c = {"if": rest, "ifdef": "defined(%s)" % rest, "ifndef": "!defined(%s)" % rest}[d]
            if d == "ifndef" and self._guard and rest == self._guard and not self.cond_stack:
                c = ""   # include-guard заголовка — условие не считается
            self.cond_stack.append(c)
            self.cond_hist.append([c])
        elif d in ("elif", "else", "elifdef", "elifndef"):
            if not self.cond_stack:
                self.errors.append("#%s без #if на строке %d" % (d, t.line))
                return
            prev = self.cond_hist[-1]
            neg = " && ".join("!(%s)" % p for p in prev if p)
            if d == "else":
                c = neg or "else"
            else:
                body = rest if d == "elif" else ("defined(%s)" % rest if d == "elifdef" else "!defined(%s)" % rest)
                c = (neg + " && " if neg else "") + "(%s)" % body
                prev.append(body)
            self.cond_stack[-1] = c
        elif d == "endif":
            if not self.cond_stack:
                self.errors.append("#endif без #if на строке %d" % t.line)
                return
            self.cond_stack.pop()
            self.cond_hist.pop()
        elif d in ("include", "define", "undef", "pragma", "error", "warning", "line", "import"):
            kind = {"include": "include", "define": "define", "undef": "undef"}.get(d, "pragma")
            it = Item(kind, self.name, t, t, self._cond(), self._ns(scope))
            it.toks = []
            it.line_end = t.line + t.text.count("\n")
            if kind == "include":
                it.name = rest
                it.key = "include " + rest
            elif kind in ("define", "undef"):
                nm = re.match(r"[A-Za-z_][A-Za-z0-9_]*", rest)
                it.name = nm.group(0) if nm else rest
                it.key = "%s %s | %s" % (kind, it.name, it.cond)
            else:
                it.name = "#" + d + " " + rest
                it.key = "pp " + d + " " + rest + " | " + it.cond
            it.cmp_text = "\n".join(ln.rstrip() for ln in t.text.split("\n"))
            if not (kind == "pragma" and rest == "once"):
                self.items.append(it)

    def _bare_macro(self, code, i):
        """Голый вызов макроса без скобок и ';' в отдельной строке (например PRAGMA_DISABLE_..._WARNINGS)."""
        t = code[i]
        if t.kind != "id" or not _is_macro_name(t.text) or t.text in SPECIFIER_MACROS:
            return False
        if i + 1 >= len(code):
            return True
        nx = code[i + 1]
        return nx.nl_before and nx.text not in ("(", "::", "<", ";", "=")

    def _find_semicolon(self, code, k):
        """Индекс ';' на нулевой глубине начиная с k."""
        depth = 0
        while k < len(code):
            tx = code[k].text
            if code[k].kind == "punct":
                if tx in "([{":
                    depth += 1
                elif tx in ")]}":
                    depth -= 1
                elif tx == ";" and depth == 0:
                    return k
            k += 1
        raise ParseError("нет ';' до конца файла")

    def _after(self, it, code):
        """Индекс токена после элемента."""
        return it._end_idx + 1

    def _mk_simple(self, kind, code, i, k, scope):
        """Создать элемент из токенов [i..k]."""
        it = Item(kind, self.name, code[i], code[k], self._cond(), self._ns(scope))
        it.toks = code[i:k + 1]
        it._end_idx = k
        it.line_end = code[k].line + code[k].text.count("\n")
        self._finish(it, scope)
        return it

    def _skip_balanced(self, code, k):
        """С '{'/'('/'[' на позиции k — вернуть индекс парной закрывающей скобки."""
        pairs = {"(": ")", "[": "]", "{": "}"}
        stack = [pairs[code[k].text]]
        k += 1
        while k < len(code):
            t = code[k]
            if t.kind == "punct":
                if t.text in pairs:
                    stack.append(pairs[t.text])
                elif t.text in (")", "]", "}"):
                    if not stack or stack[-1] != t.text:
                        raise ParseError("несбалансированная скобка '%s' на строке %d" % (t.text, t.line))
                    stack.pop()
                    if not stack:
                        return k
            k += 1
        raise ParseError("не закрыта скобка до конца файла (строка %d)" % code[k - 1].line)

    def _statement(self, code, i, scope):
        """Разобрать одно объявление/определение верхнего уровня начиная с токена i; вернуть индекс следующего."""
        n = len(code)
        k = i
        paren0_open = None     # индекс первой '(' на нулевой глубине
        paren0_close = None    # индекс парной ей ')'
        seen_eq = False        # встречен '=' на нулевой глубине
        ctor = False           # идёт список инициализации конструктора
        typekw = False         # в заголовке до '(' есть class/struct/union/enum
        brace_consumed = False  # уже было тело в фигурных скобках (тип/инициализатор)
        cond = self._cond()
        ns = self._ns(scope)
        first = code[i]
        while k < n:
            t = code[k]
            if t.kind == "pp":
                d, _ = _pp_parse(t.text)
                if d in ("if", "ifdef", "ifndef", "elif", "else", "endif", "elifdef", "elifndef"):
                    self._handle_pp(t, scope)
                k += 1
                continue
            tx = t.text
            if t.kind == "id" and tx in TYPE_KEYWORDS and paren0_open is None and not seen_eq:
                typekw = True
            if t.kind == "punct":
                if tx in ("(", "["):
                    if tx == "(" and paren0_open is None:
                        paren0_open = k
                    e = self._skip_balanced(code, k)
                    if tx == "(" and paren0_close is None and paren0_open == k:
                        paren0_close = e
                        # вызов макроса без ';' (IMPLEMENT_MODULE(...), UE_..._DEFINE(...)) — ALL_CAPS(…) и перевод строки
                        if (k == i + 0 + 1 and _is_macro_name(code[i].text)) and (
                                e + 1 >= n or (code[e + 1].nl_before and code[e + 1].text not in CONT_AFTER_PAREN)):
                            it = Item("macro_call", self.name, first, code[e], cond, ns)
                            it.toks = code[i:e + 1]
                            it._end_idx = e
                            it.line_end = code[e].line
                            self._finish(it, scope)
                            return e + 1
                    k = e + 1
                    continue
                if tx == "=":
                    seen_eq = True
                elif tx == ":" and paren0_close is not None and not seen_eq:
                    ctor = True
                elif tx == ";":
                    it = Item(self._classify_semicolon(code, i, k, paren0_open, paren0_close, seen_eq,
                                                       typekw, brace_consumed), self.name, first, t, cond, ns)
                    it.toks = code[i:k + 1]
                    it._end_idx = k
                    it.line_end = t.line
                    self._finish(it, scope)
                    return k + 1
                elif tx == "}":
                    raise ParseError("неожиданная '}' на строке %d" % t.line)
                elif tx == "{":
                    e = self._skip_balanced(code, k)
                    is_func = (paren0_open is not None and not seen_eq and not (typekw and paren0_open > k))
                    if typekw and (paren0_open is None or paren0_open > k):
                        is_func = False
                    if is_func and ctor and code[k - 1].kind == "id" or (is_func and ctor and code[k - 1].text == ">"):
                        k = e + 1           # инициализатор члена в фигурных скобках
                        continue
                    if is_func:
                        it = Item("func", self.name, first, code[e], cond, ns)
                        it.toks = code[i:e + 1]
                        it._end_idx = e
                        it.line_end = code[e].line
                        it._paren = (paren0_open, paren0_close)
                        it._body = k
                        self._finish(it, scope)
                        return e + 1
                    brace_consumed = True
                    k = e + 1
                    continue
            k += 1
        raise ParseError("объявление со строки %d не закончено до конца файла" % first.line)

    def _classify_semicolon(self, code, i, k, p_open, p_close, seen_eq, typekw, brace_consumed):
        """Определить вид элемента, закончившегося ';'."""
        hdr = [t for t in code[i:k] if t.kind != "pp"]
        words = [t.text for t in hdr]
        w0 = words[0] if words else ""
        if w0 in ("using", "typedef"):
            return "alias"
        if w0 in ("static_assert",) or (w0 == "UE_STATIC_ASSERT"):
            return "assert"
        if "extern" in words[:3] and not seen_eq:
            return "decl"
        if typekw and brace_consumed and not seen_eq:
            return "type"
        if (w0 in TYPE_KEYWORDS or (w0 == "template" and any(w in TYPE_KEYWORDS for w in words))) and not brace_consumed \
                and not seen_eq and p_open is None:
            return "decl"
        nonstore = [w for w in words if w not in STORAGE_TOKENS]
        if nonstore and _is_macro_name(nonstore[0]) and p_open is not None and code[p_open - 1].text == nonstore[0]:
            return "macro_call"
        if p_open is not None and not seen_eq:
            args = code[p_open + 1:p_close]
            # ★ R3c-b 12.11.2026: «(» внутри аргументов шаблона — признак ТИПА параметра (TFunction<void()>), а не
            #   выражения инициализатора: прототип «static void F(float, TFunction<void()> Fn);» считался объектом,
            #   и эмиттер писал «extern void F;» (C2182). Уровень угловых скобок считается только до первой «(» вне шаблона.
            lit, angle = False, 0
            for t in args:
                if t.kind == "punct" and t.text == "<":
                    angle += 1
                elif t.kind == "punct" and t.text == ">" and angle:
                    angle -= 1
                elif t.kind == "punct" and t.text == ">>" and angle:
                    angle = max(0, angle - 2)
                if t.kind in ("str", "num", "chr") or t.text in ("{", "TEXT") or (t.text == "(" and not angle):
                    lit = True
                    break
            # аргумент — квалифицированное имя без признаков типа («T x(UAsdaCraftSubsystem::kMainWindowId);») — это
            #   инициализация объекта, а не прототип (урок пилота R2 10.11: константа считалась объявлением и не делилась)
            params, cur = [], []
            for t in args:
                if t.text == "," and t.kind == "punct":
                    params.append(cur)
                    cur = []
                else:
                    cur.append(t)
            params.append(cur)
            qual = any(len(p) >= 3 and "::" in [t.text for t in p]
                       and all(t.kind == "id" or t.text == "::" for t in p)
                       and not any(t.text in ("const", "unsigned", "signed", "struct", "class", "enum") for t in p)
                       and p[-1].kind == "id" and p[-2].text == "::" for p in params)
            if not lit and not qual:
                return "decl"
        return "object"

    def _finish(self, it, scope):
        """Вычислить ключ, имя, текст сверки и признаки элемента; добавить в список."""
        toks = [t for t in it.toks if t.kind != "pp"]
        storage = []
        keep = []
        cut_spans = []          # диапазоны текста, вырезаемые при сверке (спецификаторы связывания/атрибуты)
        k = 0
        # заголовок = до тела/'='/'(' аргументов объекта
        head_end = len(toks)
        if it.kind == "func":
            body_tok = None
            for x, t in enumerate(toks):
                if t.start == self._tok_start_of_body(it):
                    body_tok = x
                    break
            head_end = body_tok if body_tok is not None else len(toks)
        while k < head_end:
            t = toks[k]
            if t.kind == "id" and t.text in STORAGE_TOKENS:
                storage.append(t.text)
                cut_spans.append((t.start, t.end))
                k += 1
                continue
            if t.text == "[" and k + 1 < head_end and toks[k + 1].text == "[":
                depth = 0
                m = k
                while m < head_end:
                    if toks[m].text == "[":
                        depth += 1
                    elif toks[m].text == "]":
                        depth -= 1
                        if depth == 0:
                            break
                    m += 1
                storage.append("".join(x.text for x in toks[k:m + 1]))
                cut_spans.append((toks[k].start, toks[m].end))
                k = m + 1
                continue
            keep.append(t)
            k += 1
        it.storage = tuple(storage)
        if it.kind == "func":
            # ключ: заголовок без спецификаторов и без списка инициализации конструктора
            hk = []
            depth = 0
            for t in keep:
                if t.kind == "punct" and t.text in "([{":
                    depth += 1
                elif t.kind == "punct" and t.text in ")]}":
                    depth -= 1
                if depth == 0 and t.text == ":" and hk and hk[-1] == ")":
                    break
                hk.append(t.text)
            it.key = "func %s::%s | %s" % (it.ns, _norm_join([_FakeTok(x) for x in hk]), it.cond)
            # имя — цепочка идентификаторов перед первой '(' заголовка
            names = []
            for x, t in enumerate(keep):
                if t.text == "(":
                    m = x - 1
                    want_id = True      # ожидаем идентификатор (иначе — '::' или '~'); два id подряд = конец имени
                    while m >= 0:
                        tm = keep[m]
                        if want_id and tm.kind == "id":
                            names.insert(0, tm.text)
                            want_id = False
                        elif not want_id and tm.text == "::":
                            names.insert(0, tm.text)
                            want_id = True
                        elif tm.text == "~" or (want_id and tm.text == ">"):
                            names.insert(0, tm.text)
                            if tm.text == ">":
                                break
                        else:
                            break
                        m -= 1
                    if names and names[-1] == "operator":
                        names.append("()")
                    break
            it.name = "".join(names) or _norm_join(keep)[:60]
            body = self.text[self._tok_start_of_body(it):it.end]
            it.local_static = re.search(r"(?<![A-Za-z0-9_])static(?![A-Za-z0-9_])(?!_assert|_cast)", _strip_lits(body)) is not None
        else:
            dk = []
            depth = 0
            for t in keep:
                if depth == 0 and t.kind == "punct" and t.text in ("=", "(", "{", ";") and it.kind not in ("alias", "decl", "assert"):
                    break
                if t.kind == "punct" and t.text in "([{":
                    depth += 1
                elif t.kind == "punct" and t.text in ")]}":
                    depth -= 1
                dk.append(t)
            if it.kind in ("macro_call", "alias", "decl", "assert", "type"):
                dk = keep if it.kind != "type" else [t for t in keep if t.text != ";"][:_upto_brace(keep)]
            it.key = "%s %s::%s | %s" % (it.kind, it.ns, _norm_join(dk), it.cond)
            ids = [t.text for t in dk if t.kind == "id" and t.text not in STORAGE_TOKENS]
            if it.kind == "macro_call":
                it.name = _norm_join(keep)[:80]
            elif it.kind == "type":
                it.name = " ".join(t.text for t in dk if t.kind == "id")[:80]
            else:
                it.name = ids[-1] if ids else _norm_join(dk)[:60]
                if it.kind == "decl":
                    # прототип «T Name(параметры);» — имя перед первой '(' (а не последний идентификатор — имя параметра;
                    #   урок R2 Gacha 11.11: прототип внешней функции не считался используемым и не попал в заголовок)
                    for x, t in enumerate(dk):
                        if t.text == "(" and x > 0 and dk[x - 1].kind == "id":
                            it.name = dk[x - 1].text
                            break
            head = [t.text for t in dk]
            it.mutable = it.kind == "object" and "const" not in head and "constexpr" not in head and "constinit" not in head
            it.registration = it.kind in ("object", "macro_call") and bool(REG_RE.search(_norm_join(dk)))
        # текст сверки: исходный срез без спецификаторов связывания
        raw = self.text[it.start:it.end]
        if cut_spans:
            parts = []
            pos = it.start
            for a, b in sorted(cut_spans):
                parts.append(self.text[pos:a])
                pos = b
                while pos < it.end and self.text[pos] in " \t":
                    pos += 1
            parts.append(self.text[pos:it.end])
            raw = "".join(parts)
        it.cmp_text = _dedent_rstrip(raw)
        self.items.append(it)

    def _tok_start_of_body(self, it):
        """Смещение '{' тела функции."""
        return self._code_cache()[it._body].start

    def _code_cache(self):
        """Список токенов без комментариев (кэш)."""
        if not hasattr(self, "_code"):
            self._code = [t for t in self.all_toks if t.kind != "comment"]
        return self._code


class _FakeTok:
    """Обёртка строки под интерфейс токена (для _norm_join)."""
    __slots__ = ("text", "kind")

    def __init__(self, text):
        self.text = text   # текст
        self.kind = "id"   # вид (не комментарий/директива)


def _upto_brace(toks):
    """Число токенов до первой '{' (заголовок типа)."""
    for x, t in enumerate(toks):
        if t.text == "{":
            return x
    return len(toks)


def _strip_lits(s):
    """Убрать из текста строки, символы и комментарии (для поиска ключевых слов в теле)."""
    s = re.sub(r'R"([^(\s]*)\(.*?\)\1"', '""', s, flags=re.S)
    s = re.sub(r'"(?:\\.|[^"\\\n])*"', '""', s)
    s = re.sub(r"'(?:\\.|[^'\\\n])*'", "''", s)
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    return s


# ─────────────────────────────── сверка ───────────────────────────────

# Виды, которые должны присутствовать «после» РОВНО один раз с тем же текстом.
EXACT_KINDS = {"func", "type", "object", "macro_call", "case"}
# Виды, которые должны присутствовать хотя бы раз (повтор — предупреждение).
ATLEAST_KINDS = {"define", "undef", "alias", "assert", "pragma"}
# Виды, свободные при разрезании (появление/пропажа — только справка).
FREE_KINDS = {"include", "decl"}


class Report:
    """Накопитель результатов сверки."""

    def __init__(self):
        self.errors = []      # (код, текст) — делают код возврата 1
        self.warnings = []    # (код, текст) — требуют взгляда, но не валят проверку
        self.info = []        # (код, текст) — справка
        self.reg_table = []   # строки таблицы регистраций/статических объектов
        self.stats = {}       # числа

    def err(self, code, msg):
        """Добавить ошибку."""
        self.errors.append((code, msg))

    def warn(self, code, msg):
        """Добавить предупреждение."""
        self.warnings.append((code, msg))

    def note(self, code, msg):
        """Добавить справку."""
        self.info.append((code, msg))

    def render(self, title):
        """Отчёт в виде текста Markdown."""
        out = ["# asda_split_check — %s" % title, ""]
        out.append("Итог: **%s** — ошибок %d, предупреждений %d." % (
            "ЧИСТО" if not self.errors else "РАСХОЖДЕНИЯ", len(self.errors), len(self.warnings)))
        out.append("")
        if self.stats:
            out.append("Числа: " + ", ".join("%s=%s" % kv for kv in self.stats.items()))
            out.append("")
        for hdr, lst in (("Ошибки", self.errors), ("Предупреждения", self.warnings), ("Справка", self.info)):
            if not lst:
                continue
            out.append("## %s (%d)" % (hdr, len(lst)))
            for code, msg in lst:
                out.append("- [%s] %s" % (code, msg))
            out.append("")
        if self.reg_table:
            out.append("## Статические объекты и регистрации: «до» → «после» (риск порядка инициализации)")
            out.append("")
            out.append("| # до | вид | имя | условие | до (файл:строка) | после (файл:строка) |")
            out.append("|---|---|---|---|---|---|")
            out.extend(self.reg_table)
            out.append("")
        return "\n".join(out)


def _loc(it):
    """«файл:строка» элемента."""
    return "%s:%d" % (os.path.basename(it.file), it.line_start)


def _short_diff(a, b, n=6):
    """Первые строки различия двух текстов."""
    d = list(difflib.unified_diff(a.split("\n"), b.split("\n"), "до", "после", lineterm="", n=1))
    return " / ".join(x for x in d[2:2 + n])


def _key_nocond(key):
    """Ключ без условия препроцессора."""
    return key.rsplit(" | ", 1)[0]


def _key_noscope(key):
    """Ключ без пространства имён и условия (для поиска «переехавших»)."""
    k = _key_nocond(key)
    m = re.match(r"(\w+) ([^ ]*?)::(.*)", k)
    return (m.group(1) + " " + m.group(3)) if m else k


def _is_member_def(it):
    """Определение статического члена вне класса («T UAsdaX::kId(…);»): имя перед '('/'='/'{'/';' квалифицировано '::'."""
    toks = [t for t in it.toks if t.kind != "pp"]
    for x, t in enumerate(toks):
        if t.kind == "punct" and t.text in ("(", "=", "{", ";", "["):
            return x >= 2 and toks[x - 1].kind == "id" and toks[x - 2].text == "::"
    return False


PP_COMMENT_RX = re.compile(r"^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else)\b([^\n/]*?)[ \t]*(//[^\n]*)$", re.M)  # строка pp с комментарием


def pp_comment_lines(text):
    """Строки препроцессора с хвостовым комментарием: [(вид, условие без пробелов, комментарий)] (R3c-b 12.11.2026).
    Строки внутри многострочных комментариев/строк тоже попадут — для сверки «до/после» это одинаково с обеих сторон."""
    return [(k, re.sub(r"\s+", "", c), t.strip()) for k, c, t in PP_COMMENT_RX.findall(text)]


def compare(before_models, after_models, allow_dup=(), strict_linkage=False, strict_comments=False):
    """Сверить модели «до» и «после»; вернуть Report."""
    rep = Report()
    for m in before_models + after_models:
        for e in m.errors:
            rep.err("PARSE", "%s: %s" % (m.name, e))
    b_items = [it for m in before_models for it in m.items]
    a_items = [it for m in after_models for it in m.items]
    a_by_key = defaultdict(list)
    for it in a_items:
        a_by_key[it.key].append(it)
    a_by_nocond = defaultdict(list)
    a_by_noscope = defaultdict(list)
    for it in a_items:
        a_by_nocond[_key_nocond(it.key)].append(it)
        a_by_noscope[_key_noscope(it.key)].append(it)
    used = set()                         # id() элементов «после», сопоставленных с «до»
    b_keys = defaultdict(int)
    for it in b_items:
        b_keys[it.key] += 1
    matched = {}                         # id(элемент до) → элемент после
    log_static_names = set()
    for it in b_items:
        if it.kind == "macro_call" and it.name.startswith(LOG_STATIC):
            m = re.match(LOG_STATIC + r"\s*\(\s*(\w+)", it.name)
            if m:
                log_static_names.add(m.group(1))
    n_exact = n_link = 0
    for it in b_items:
        cands = a_by_key.get(it.key, [])
        if it.kind in FREE_KINDS:
            if not cands:
                if it.kind == "decl":
                    rep.note("DECL_GONE", "объявление «до» не найдено «после»: %s (%s)" % (_norm_join(it.toks)[:120], _loc(it)))
            for c in cands:
                used.add(id(c))
            continue
        if it.kind == "macro_call" and it.name.startswith(LOG_STATIC):
            nm = re.match(LOG_STATIC + r"\s*\(\s*(\w+)", it.name)
            nm = nm.group(1) if nm else ""
            if cands:
                pass
            else:
                dfn = [a for a in a_items if a.kind == "macro_call" and re.match(r"DEFINE_LOG_CATEGORY\s*\(\s*%s\b" % nm, a.name)]
                dcl = [a for a in a_items if a.kind == "macro_call" and re.match(r"DECLARE_LOG_CATEGORY_EXTERN\s*\(\s*%s\b" % nm, a.name)]
                if len(dfn) == 1:
                    used.update(id(a) for a in dfn + dcl)
                    rep.warn("LOGCAT", "%s: DEFINE_LOG_CATEGORY_STATIC → DEFINE_LOG_CATEGORY (%s) + DECLARE_LOG_CATEGORY_EXTERN (%s) — "
                             "связывание категории стало внешним; проверить, что имя не занято в другом .cpp модуля"
                             % (nm, _loc(dfn[0]), ", ".join(_loc(a) for a in dcl) or "нет"))
                    matched[id(it)] = dfn[0]
                    continue
        if not cands:
            alt = [a for a in a_by_nocond.get(_key_nocond(it.key), []) if id(a) not in used]
            if alt:
                rep.err("COND", "%s «%s»: условие препроцессора изменилось: до «%s» (%s) → после «%s» (%s)"
                        % (it.kind, it.name, it.cond or "—", _loc(it), alt[0].cond or "—", _loc(alt[0])))
                used.add(id(alt[0]))
                continue
            alt = [a for a in a_by_noscope.get(_key_noscope(it.key), []) if id(a) not in used]
            if alt:
                a0 = alt[0]
                no_anon = "::".join(n for n in it.ns.split("::") if n and n != "(anon)")
                if "(anon)" in it.ns and a0.ns == no_anon and a0.cmp_text == it.cmp_text:
                    # вынос общего объекта из анонимного пространства имён (extern на внутреннее связывание невозможен):
                    #   текст тот же, меняется только связывание — как при снятии static (R2 11.11, PremiumShop)
                    (rep.err if strict_linkage else rep.warn)("LINKAGE", "%s «%s»: вынесен из анонимного пространства имён "
                                                              "(%s → %s)" % (it.kind, it.name, _loc(it), _loc(a0)))
                    matched[id(it)] = a0
                    n_exact += 1
                    n_link += 1
                else:
                    rep.err("NS", "%s «%s»: пространство имён изменилось: до «%s» (%s) → после «%s» (%s)"
                            % (it.kind, it.name, it.ns or "::", _loc(it), a0.ns or "::", _loc(a0)))
                used.add(id(a0))
                continue
            if it.kind in ATLEAST_KINDS:
                if it.kind == "pragma":
                    rep.warn("MISSING_PP", "директива пропала: %s (%s)" % (it.name, _loc(it)))
                else:
                    rep.err("MISSING", "%s «%s» не найден «после» (%s)" % (it.kind, it.name, _loc(it)))
                continue
            if it.kind == "object" and any(a.kind == "decl" for a in a_items if a.name == it.name):
                pass
            rep.err("MISSING", "%s «%s» не найден «после» (%s)" % (it.kind, it.name, _loc(it)))
            continue
        free = [c for c in cands if id(c) not in used]
        same = [c for c in free if c.cmp_text == it.cmp_text]
        if b_keys[it.key] > 1:
            # одинаковый ключ «до» несколько раз (редко: одинаковые макровызовы) — сопоставлять по тексту
            pick = same[0] if same else (free[0] if free else None)
        else:
            pick = same[0] if same else (free[0] if free else cands[0])
            extra_same = [c for c in cands if c is not pick]
            if extra_same and it.kind in EXACT_KINDS and it.name not in allow_dup:
                rep.err("DUPLICATE", "%s «%s» встречается «после» %d раз: %s" % (
                    it.kind, it.name, len(cands), ", ".join(_loc(c) for c in cands)))
            elif extra_same:
                rep.warn("DUP", "%s «%s» встречается «после» %d раз: %s" % (
                    it.kind, it.name, len(cands), ", ".join(_loc(c) for c in cands)))
            for c in extra_same:
                used.add(id(c))
        if pick is None:
            rep.err("MISSING", "%s «%s» (повтор ключа) не найден «после» (%s)" % (it.kind, it.name, _loc(it)))
            continue
        used.add(id(pick))
        matched[id(it)] = pick
        if pick.cmp_text != it.cmp_text:
            rep.err("CHANGED", "%s «%s» (%s → %s): текст изменён: %s" % (
                it.kind, it.name, _loc(it), _loc(pick), _short_diff(it.cmp_text, pick.cmp_text)))
            continue
        n_exact += 1
        if set(it.storage) != set(pick.storage):
            n_link += 1
            msg = "%s «%s»: спецификаторы «%s» → «%s» (%s → %s)" % (
                it.kind, it.name, " ".join(it.storage) or "—", " ".join(pick.storage) or "—", _loc(it), _loc(pick))
            (rep.err if strict_linkage else rep.warn)("LINKAGE", msg)
    # лишнее «после»
    for it in a_items:
        if id(it) in used:
            continue
        if it.kind in FREE_KINDS:
            if it.kind == "decl":
                rep.note("DECL_NEW", "новое объявление: %s (%s)" % (_norm_join(it.toks)[:120], _loc(it)))
            continue
        if it.kind == "macro_call" and re.match(r"DECLARE_LOG_CATEGORY_EXTERN", it.name):
            rep.note("LOGCAT_DECL", "объявление категории журнала: %s (%s)" % (it.name, _loc(it)))
            continue
        if it.kind in ("define", "undef", "alias", "assert", "pragma") and any(
                _key_nocond(b.key) == _key_nocond(it.key) for b in b_items):
            rep.warn("DUP", "%s «%s» повторён «после» (%s)" % (it.kind, it.name, _loc(it)))
            continue
        rep.err("EXTRA", "%s «%s» появился «после», «до» его нет (%s)" % (it.kind, it.name, _loc(it)))
    # состояние в общем заголовке
    for m in after_models:
        if not m.is_header:
            continue
        for it in m.items:
            internal = "static" in it.storage or "(anon)" in it.ns
            if it.kind == "object" and it.registration and (internal or True):
                rep.err("HEADER_REG", "объект регистрации в заголовке «%s» (%s): зарегистрируется в КАЖДОЙ части — "
                        "определение должно остаться в одном .cpp, в заголовке только extern" % (it.name, _loc(it)))
            elif it.kind == "object" and _is_member_def(it):
                rep.err("HEADER_ODR", "определение статического члена класса в заголовке «%s» (%s): по копии в каждой части "
                        "→ LNK2005; оставить в одном .cpp (класс уже объявляет его)" % (it.name, _loc(it)))
            elif it.kind == "object" and it.mutable and internal:
                rep.err("HEADER_STATE", "изменяемый file-static объект в заголовке «%s» (%s): у каждой части будет своя копия "
                        "состояния — определение в один .cpp, в заголовок extern (или const, если это константа)" % (it.name, _loc(it)))
            elif it.kind == "object" and it.mutable and not internal and "extern" not in [t.text for t in it.toks]:
                rep.err("HEADER_ODR", "внешний объект определён в заголовке «%s» (%s): повторное определение при сборке" % (it.name, _loc(it)))
            elif it.kind == "func" and it.local_static and (internal and "inline" not in it.storage):
                rep.err("HEADER_LOCAL_STATIC", "static-функция с локальной static в заголовке «%s» (%s): у каждой части своя "
                        "копия локального состояния — сделать inline (одна копия на программу) или оставить в одном .cpp" % (it.name, _loc(it)))
            elif it.kind == "func" and not internal and "inline" not in it.storage and "FORCEINLINE" not in it.storage \
                    and "template" not in [t.text for t in it.toks[:1]]:
                rep.err("HEADER_ODR", "функция с внешним связыванием определена в заголовке «%s» (%s): повторное определение "
                        "в каждой части — нужен static или inline" % (it.name, _loc(it)))
    # порядок статических объектов внутри каждого файла «после» и таблица регистраций
    b_index = {id(it): x for x, it in enumerate(b_items)}
    by_after_file = defaultdict(list)
    statics = [it for it in b_items if it.kind in ("object", "macro_call") and (it.mutable or it.registration or it.kind == "macro_call")]
    for it in statics:
        a = matched.get(id(it))
        if a is None:
            continue
        by_after_file[a.file].append((a.start, b_index[id(it)], it, a))
        if it.registration or it.kind == "object":
            rep.reg_table.append("| %d | %s | %s | %s | %s | %s |" % (
                b_index[id(it)], it.kind, it.name.replace("|", "\\|")[:60], (it.cond or "—").replace("|", "\\|")[:50],
                _loc(it), _loc(a)))
    for f, lst in by_after_file.items():
        lst.sort()
        mx = -1
        mx_it = None
        for _, bi, it, a in lst:
            if bi < mx:
                rep.err("ORDER", "порядок статических объектов в %s нарушен: «%s» (до #%d) стоит после «%s» (до #%d)"
                        % (os.path.basename(f), it.name, bi, mx_it.name, mx))
            if bi > mx:
                mx, mx_it = bi, it
    # зависимость инициализаторов между разными файлами «после»
    st_names = {it.name: it for it in statics if it.name and re.fullmatch(r"[A-Za-z_]\w*", it.name)}
    for it in statics:
        a = matched.get(id(it))
        if a is None:
            continue
        txt = _strip_lits(it.cmp_text)
        for nm, other in st_names.items():
            if other is it or len(nm) < 3:
                continue
            if re.search(r"(?<!\w)%s(?!\w)" % re.escape(nm), txt.split("=", 1)[-1] if "=" in txt else txt[txt.find("("):] if "(" in txt else ""):
                oa = matched.get(id(other))
                if oa is not None and oa.file != a.file:
                    rep.warn("ORDER_XTU", "инициализатор «%s» (%s) ссылается на «%s» (%s) из ДРУГОЙ единицы трансляции — "
                             "порядок статической инициализации между файлами не определён" % (it.name, _loc(a), nm, _loc(oa)))
    # комментарии верхнего уровня
    from collections import Counter
    bc = Counter(c for m in before_models for c, _ in m.top_comments)
    ac = Counter(c for m in after_models for c, _ in m.top_comments)
    lost = bc - ac
    if lost:
        first_lines = defaultdict(int)
        for m in before_models:
            for c, ln in m.top_comments:
                if c in lost and not first_lines.get(c):
                    first_lines[c] = ln
        for c, cnt in lost.items():
            (rep.err if strict_comments else rep.warn)("COMMENT_LOST", "комментарий верхнего уровня пропал (строка %d «до»): %s"
                                                      % (first_lines.get(c, 0), c.split("\n")[0][:100]))
    added = ac - bc
    if added:
        rep.note("COMMENT_NEW", "новых комментариев верхнего уровня (пометки разноса): %d" % sum(added.values()))
    # ★ R3c-b 12.11.2026: хвостовые комментарии строк препроцессора (#if/#ifdef/#ifndef/#elif/#else; #endif — нет:
    #   эмиттер пишет «#endif // УСЛОВИЕ» сам). Эмиттеры R2 склеивали соседние одинаковые обёртки и собирали строку «#if»
    #   заново — свой комментарий у строки терялся, подменялся чужим или к голому «#if» прилипала следующая строка;
    #   сравнение условий (COND) этого не видит. ПОТЕРЯН — строк с этим (вид, условие, комментарий) «после» меньше, чем
    #   «до»; ЧУЖОЙ — комментарий строки препроцессора «после», которого ни на одной такой строке «до» нет.
    pb = Counter(k for m in before_models for k in pp_comment_lines(m.text))
    pa = Counter(k for m in after_models for k in pp_comment_lines(m.text))
    for (kind, cond, c), cnt in sorted((pb - pa).items()):
        (rep.err if strict_comments else rep.warn)(
            "PP_COMMENT_LOST", "строка «#%s %s   %s» — «после» на %d меньше, чем «до»" % (kind, cond, c[:100], cnt))
    b_txt = {(k, c) for k, _, c in pb}
    for (kind, cond, c), cnt in sorted(pa.items()):
        if (kind, c) not in b_txt:
            where = [m.name for m in after_models if (kind, cond, c) in pp_comment_lines(m.text)]
            (rep.err if strict_comments else rep.warn)(
                "PP_COMMENT_FOREIGN", "строка «#%s %s   %s» — такого комментария у строк препроцессора «до» нет (%s)"
                % (kind, cond, c[:100], ", ".join(os.path.basename(w) for w in where)))
    # includes
    b_inc = {it.name for it in b_items if it.kind == "include"}
    a_inc = {it.name for it in a_items if it.kind == "include"}
    for x in sorted(b_inc - a_inc):
        rep.note("INCLUDE_GONE", "#include %s был «до», «после» его нет ни в одной части" % x)
    rep.stats = {"элементов_до": len(b_items), "элементов_после": len(a_items),
                 "функций_до": sum(1 for x in b_items if x.kind == "func"),
                 "функций_после": sum(1 for x in a_items if x.kind == "func"),
                 "совпало_дословно": n_exact, "смена_связывания": n_link,
                 "статических_объектов": len(statics)}
    return rep


# ─────────────────────────────── разбор switch по группам case ───────────────────────────────

class CaseGroup:
    """Группа меток case/default с общим телом (до следующей метки того же switch)."""

    def __init__(self):
        self.labels = []        # тексты меток (нормализованные), например «OP_NET_RECV_LOGIN_RESPONSE», «(NetOpcode)4101»
        self.start = 0          # смещение начала (строка первой метки)
        self.end = 0            # смещение конца (не включая)
        self.line_start = 0     # строка первой метки
        self.line_end = 0       # последняя строка
        self.text = ""          # нормализованный текст группы (метки + тело)
        self.comment = ""       # комментарий в строке первой метки
        self.has_return = False  # в теле есть return (выход из ВСЕЙ функции — важно при выносе в подфункцию)
        self.has_goto = False   # в теле есть goto
        self.fallthrough = False  # тело не заканчивается break/return/continue/throw — возможно сквозное выполнение
        self.bytes = 0          # размер в байтах UTF-8

    def to_dict(self):
        """Представление для JSON."""
        return {"labels": self.labels, "line_start": self.line_start, "line_end": self.line_end, "bytes": self.bytes,
                "comment": self.comment, "has_return": self.has_return, "has_goto": self.has_goto,
                "fallthrough": self.fallthrough}


def _ends_with_terminal(words):
    """Последний оператор тела (с учётом завершающих '}') — break/return/continue/throw."""
    w = list(words)
    while w and w[-1] in ("}", ";"):
        w.pop()
    for x in reversed(w[-40:]):
        if x in ("break", "return", "continue", "throw"):
            return True
        if x in (";", "{", "}"):
            return False
    return False


def switch_cases(fm, item):
    """Разобрать САМЫЙ БОЛЬШОЙ switch на первом уровне тела функции item: (группы, пролог, эпилог, заголовок switch).

    Пролог — текст тела функции до switch, эпилог — после его закрывающей скобки. Метки ищутся только на уровне
    самого switch (вложенные switch внутри case не трогаются)."""
    code = [t for t in fm.all_toks if t.kind != "comment" and item.start <= t.start < item.end]
    comments = [t for t in fm.all_toks if t.kind == "comment" and item.start <= t.start < item.end]
    body_off = fm._tok_start_of_body(item)
    body_k = next(x for x, t in enumerate(code) if t.start == body_off)
    depth = 0
    best = None              # (размер, индекс 'switch', индекс '{', индекс '}')
    k = body_k
    while k < len(code):
        t = code[k]
        if t.kind == "punct" and t.text == "{":
            depth += 1
        elif t.kind == "punct" and t.text == "}":
            depth -= 1
        elif t.kind == "id" and t.text == "switch" and depth == 1 and k + 1 < len(code) and code[k + 1].text == "(":
            m = fm._skip_balanced(code, k + 1) + 1          # после (условия)
            if m < len(code) and code[m].text == "{":
                e = fm._skip_balanced(code, m)
                size = code[e].end - code[m].start
                if best is None or size > best[0]:
                    best = (size, k, m, e)
        k += 1
    if best is None:
        return [], "", "", ""
    _, sw, ob, cb = best
    text = fm.text
    label_pos = []           # (индекс токена case/default, индекс ':' после метки)
    d = 0
    k = ob + 1
    while k < cb:
        t = code[k]
        if t.kind == "punct" and t.text in "([{":
            d += 1
        elif t.kind == "punct" and t.text in ")]}":
            d -= 1
        elif d == 0 and t.kind == "id" and t.text in ("case", "default"):
            m = k + 1
            pd = 0
            while m < cb:
                tm = code[m]
                if tm.kind == "punct" and tm.text in "([":
                    pd += 1
                elif tm.kind == "punct" and tm.text in ")]":
                    pd -= 1
                elif pd == 0 and tm.kind == "punct" and tm.text == ":":
                    break
                m += 1
            label_pos.append((k, m))
            k = m + 1
            continue
        k += 1
    groups = []
    cur = None
    for x, (ls, colon) in enumerate(label_pos):
        nxt = label_pos[x + 1][0] if x + 1 < len(label_pos) else cb
        body_toks = code[colon + 1:nxt]
        lbl = _norm_join(code[ls + 1:colon]) if code[ls].text == "case" else "default"
        if cur is None:
            cur = CaseGroup()
            cur.start = text.rfind("\n", 0, code[ls].start) + 1
            cur.line_start = code[ls].line
            cmt = [c for c in comments if c.line == code[colon].line and c.start > code[colon].start]
            cur.comment = cmt[0].text.lstrip("/* ").strip() if cmt else ""
        cur.labels.append(lbl)
        if not body_toks:
            continue                       # метка без тела — общая группа со следующей
        nxt_off = code[nxt].start
        cur.end = text.rfind("\n", 0, nxt_off) + 1
        if cur.end <= body_toks[-1].start:
            cur.end = body_toks[-1].end
        cur.line_end = body_toks[-1].line
        cur.text = _dedent_rstrip(text[cur.start:cur.end])
        words = [t.text for t in body_toks]
        cur.has_return = "return" in words
        cur.has_goto = "goto" in words
        cur.fallthrough = not _ends_with_terminal(words)
        cur.bytes = len(text[cur.start:cur.end].encode("utf-8"))
        groups.append(cur)
        cur = None
    pro = _dedent_rstrip(text[body_off + 1:code[sw].start])
    epi = _dedent_rstrip(text[code[cb].end:item.end - 1])
    head = _norm_join(code[sw:ob])
    return groups, pro, epi, head


class _OffTok:
    """Псевдотокен с позицией (для создания Item из смещений)."""
    __slots__ = ("start", "end", "line")

    def __init__(self, off, line):
        self.start = off   # смещение
        self.end = off     # смещение конца
        self.line = line   # строка


# Служебная ветка подфункции при разрезании switch по областям: «default: bHandled = false; break;» — не из исходника.
PROTOCOL_DEFAULT_RE = re.compile(r"default\s*:\s*\{?\s*\w+\s*=\s*false\s*;\s*break\s*;\s*\}?")


def explode_switch(models, func_prefix):
    """Заменить в моделях функции с именем (без класса), начинающимся на func_prefix, группами case их главного switch.

    Возвращает список (модель, функция, пролог, эпилог, заголовок switch, число групп) для отчёта."""
    hosts = []
    for fm in models:
        new_items = []
        for it in fm.items:
            short = it.name.split("::")[-1]
            if it.kind == "func" and short.startswith(func_prefix):
                groups, pro, epi, head = switch_cases(fm, it)
                hosts.append((fm, it, pro, epi, head, len(groups)))
                for g in groups:
                    if g.labels == ["default"] and PROTOCOL_DEFAULT_RE.fullmatch(" ".join(g.text.split())):
                        continue            # служебная ветка подфункции разреза (см. карту AsdaNetSubsystem, §5.1)
                    ci = Item("case", fm.name, _OffTok(g.start, g.line_start), _OffTok(g.end, g.line_end), it.cond, it.ns)
                    ci.line_end = g.line_end
                    ci.name = "case " + " / ".join(g.labels)
                    ci.key = "case %s | %s" % (" / ".join(g.labels), it.cond)
                    ci.cmp_text = g.text
                    ci.size_bytes = g.bytes
                    new_items.append(ci)
                continue
            new_items.append(it)
        fm.items = new_items
    return hosts


def report_switch_hosts(rep, before_models, hosts):
    """Справка по функциям-носителям switch и предупреждение, если пролог/эпилог подфункций отличается от исходного."""
    b_ids = {id(fm) for fm in before_models}
    frame_b = [(h[2], h[3]) for h in hosts if id(h[0]) in b_ids]
    for fm, it, pro, epi, head, ng in hosts:
        side = "до" if id(fm) in b_ids else "после"
        rep.note("SWITCH_HOST", "%s: %s (%s:%d) — групп case %d, %s; пролог %d стр., эпилог %d стр."
                 % (side, it.name, os.path.basename(fm.name), it.line_start, ng, head,
                    pro.count("\n") + 1 if pro else 0, epi.count("\n") + 1 if epi else 0))
        if side == "после" and frame_b and (pro, epi) != frame_b[0]:
            rep.warn("SWITCH_FRAME", "%s: пролог/эпилог отличаются от исходной функции — сверить вручную "
                     "(return внутри case выходит из ПОДфункции, а не из диспетчера)" % it.name)
    for x in [x for x in rep.errors if x[0] == "EXTRA" and "func" in x[1]]:
        pass


def dump_cases(path, func, json_out=None):
    """Режим --dump FILE --cases FUNC: группы case главного switch функции (строки, байты, флаги R/G/F, метки)."""
    fm = FileModel(path, read_file(path))
    found = 0
    for it in fm.items:
        if it.kind == "func" and (it.name == func or it.name.endswith("::" + func)):
            found += 1
            groups, pro, epi, head = switch_cases(fm, it)
            print("# %s: %s — групп %d (R = есть return, G = goto, F = возможно сквозное выполнение)" % (it.name, head, len(groups)))
            for g in groups:
                print("%6d-%-6d %8d %s%s%s  %s | %s" % (g.line_start, g.line_end, g.bytes, "R" if g.has_return else "-",
                                                     "G" if g.has_goto else "-", "F" if g.fallthrough else "-",
                                                     " / ".join(g.labels)[:70], g.comment[:90]))
            if json_out:
                with open(json_out, "w", encoding="utf-8") as f:
                    json.dump({"func": it.name, "head": head, "prologue": pro, "epilogue": epi,
                               "groups": [g.to_dict() for g in groups]}, f, ensure_ascii=False, indent=1)
    return 0 if found else 2


# ─────────────────────────────── режимы ───────────────────────────────

def read_file(path):
    """Прочитать файл в UTF-8 (с BOM или без; при ошибке — cp1251)."""
    with open(path, "rb") as f:
        data = f.read()
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return data.decode("cp1251")


def load_models(paths):
    """Разобрать список файлов в модели."""
    return [FileModel(p, read_file(p)) for p in paths]


def dump(path, json_out=None, quiet=False):
    """Режим --dump: список определений верхнего уровня файла (строки, байты, вид, условие, имя)."""
    m = FileModel(path, read_file(path))
    if not quiet:
        print("# %s — элементов %d, ошибок разбора %d" % (path, len(m.items), len(m.errors)))
        for e in m.errors:
            print("ОШИБКА РАЗБОРА:", e)
        for it in m.items:
            print("%6d-%-6d %8d  %-10s %-40s %s%s" % (it.doc_line, it.line_end, it.size_bytes, it.kind,
                                                     (it.cond or "")[:40], (it.ns + "::" if it.ns else ""), it.name))
    if json_out:
        with open(json_out, "w", encoding="utf-8") as f:
            json.dump({"file": path, "errors": m.errors, "items": [it.to_dict() for it in m.items]}, f,
                      ensure_ascii=False, indent=1)
    return m


# Синтетический исходник «до» для --selftest: пространства имён, анонимное пространство, сырые строки со скобками,
# комментарии со скобками, #if ASDA_DEV_CONSOLE, конструктор с инициализатором в фигурных скобках, лямбда-объект,
# вызов макроса без ';', static-функция, структура, шаблон, консольные переменные.
SELFTEST_BEFORE = r'''// Файл-пример «до» разрезания.
#include "CoreMinimal.h"
#include "AsdaX.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaX, Log, All);

// Ключ A.
static TAutoConsoleVariable<int32> CVarAsdaA(TEXT("asda.X.A"), 1, TEXT("ключ { A }"));
// Ключ B зависит от порядка — после A.
static TAutoConsoleVariable<int32> CVarAsdaB(TEXT("asda.X.B"), 0, TEXT("ключ B"));
static bool GAsdaFlag = false;
static FAutoConsoleVariableRef CVarAsdaFlag(TEXT("asda.X.Flag"), GAsdaFlag, TEXT("флаг"));

#define ASDA_X_HELPER(a) ((a) + 1)

namespace
{
	// Сырая строка с фигурными скобками и комментарием внутри.
	const TCHAR* kRaw = TEXT(R"json({ "a": "}", // нет })json");
	// Помощник: сумма.
	int32 Sum(int32 A, int32 B) { return A + B; /* } */ }
}

// Структура-помощник.
struct FAsdaXRow
{
	int32 Id = 0;      // номер
	FString Name;      // имя
};

// Шаблонный помощник.
template <typename T>
static T Twice(T V)
{
	return V * 2;
}

// Помощник со своим состоянием.
static int32 NextSerial()
{
	static int32 Serial = 0;
	return ++Serial;
}

#if ASDA_DEV_CONSOLE
// Дев-команда.
static FAutoConsoleCommand CmdAsdaXDump(TEXT("asda.X.Dump"), TEXT("дамп"),
	FConsoleCommandDelegate::CreateLambda([]() { UE_LOG(LogAsdaX, Log, TEXT("dump %d"), Sum(1, 2)); }));
#endif

// Конструктор с инициализатором в фигурных скобках.
FAsdaX::FAsdaX(int32 InA) : A{InA}, B(InA + 1)
{
	C = ASDA_X_HELPER(A);
}

// Метод сети.
void UAsdaX::HandleA(const uint8* Data, int32 Len)
{
	if (Len < 4) { return; }
	const int32 V = *(const int32*)Data;   // значение
	UE_LOG(LogAsdaX, Log, TEXT("A=%d '}'"), V);
}

// Метод мира.
void UAsdaX::HandleB(int32 V) const
{
	auto L = [this, V]() { return V + CVarAsdaA.GetValueOnGameThread(); };
	(void)L();
}

namespace AsdaXNs
{
	// Функция в именованном пространстве.
	int32 Triple(int32 V) { return V * 3; }
}

IMPLEMENT_ASDA_THING(UAsdaX)
'''

# Правильное разрезание примера: общий заголовок + две части (дословный перенос).
SELFTEST_INTERNAL = r'''// AsdaX_Internal.h — общий заголовок частей (пример).
#pragma once
#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAsdaX, Log, All);
extern TAutoConsoleVariable<int32> CVarAsdaA;

#define ASDA_X_HELPER(a) ((a) + 1)

namespace
{
	// Сырая строка с фигурными скобками и комментарием внутри.
	const TCHAR* const kRawUnused = nullptr;
}

// Шаблонный помощник.
template <typename T>
static T Twice(T V)
{
	return V * 2;
}
'''

# Часть 1 (база): категория, ключи, анонимное пространство, структура, конструктор, команда.
SELFTEST_PART1 = r'''// Файл-пример «до» разрезания.
#include "CoreMinimal.h"
#include "AsdaX.h"
#include "AsdaX_Internal.h"

DEFINE_LOG_CATEGORY(LogAsdaX);

// Ключ A.
TAutoConsoleVariable<int32> CVarAsdaA(TEXT("asda.X.A"), 1, TEXT("ключ { A }"));
// Ключ B зависит от порядка — после A.
static TAutoConsoleVariable<int32> CVarAsdaB(TEXT("asda.X.B"), 0, TEXT("ключ B"));
static bool GAsdaFlag = false;
static FAutoConsoleVariableRef CVarAsdaFlag(TEXT("asda.X.Flag"), GAsdaFlag, TEXT("флаг"));

namespace
{
	// Сырая строка с фигурными скобками и комментарием внутри.
	const TCHAR* kRaw = TEXT(R"json({ "a": "}", // нет })json");
	// Помощник: сумма.
	int32 Sum(int32 A, int32 B) { return A + B; /* } */ }
}

// Структура-помощник.
struct FAsdaXRow
{
	int32 Id = 0;      // номер
	FString Name;      // имя
};

// Помощник со своим состоянием.
static int32 NextSerial()
{
	static int32 Serial = 0;
	return ++Serial;
}

#if ASDA_DEV_CONSOLE
// Дев-команда.
static FAutoConsoleCommand CmdAsdaXDump(TEXT("asda.X.Dump"), TEXT("дамп"),
	FConsoleCommandDelegate::CreateLambda([]() { UE_LOG(LogAsdaX, Log, TEXT("dump %d"), Sum(1, 2)); }));
#endif

// Конструктор с инициализатором в фигурных скобках.
FAsdaX::FAsdaX(int32 InA) : A{InA}, B(InA + 1)
{
	C = ASDA_X_HELPER(A);
}

IMPLEMENT_ASDA_THING(UAsdaX)
'''

# Часть 2 (сеть/мир): методы и именованное пространство.
SELFTEST_PART2 = r'''// AsdaX_Net.cpp — ★ разнос (пример): обработчики.
#include "AsdaX.h"
#include "AsdaX_Internal.h"

// Метод сети.
void UAsdaX::HandleA(const uint8* Data, int32 Len)
{
	if (Len < 4) { return; }
	const int32 V = *(const int32*)Data;   // значение
	UE_LOG(LogAsdaX, Log, TEXT("A=%d '}'"), V);
}

// Метод мира.
void UAsdaX::HandleB(int32 V) const
{
	auto L = [this, V]() { return V + CVarAsdaA.GetValueOnGameThread(); };
	(void)L();
}

namespace AsdaXNs
{
	// Функция в именованном пространстве.
	int32 Triple(int32 V) { return V * 3; }
}
'''


def _selftest_case(name, before, after, expect_codes, forbid_errors=False):
    """Один случай самотеста: разобрать, сверить, проверить ожидаемые коды. Вернуть (ok, текст)."""
    bm = [FileModel("AsdaX.cpp", before)]
    am = [FileModel(n, t) for n, t in after]
    rep = compare(bm, am)
    codes = {c for c, _ in rep.errors}
    ok = True
    why = []
    if forbid_errors and rep.errors:
        ok = False
        why.append("ожидалось ЧИСТО, ошибки: %s" % rep.errors[:5])
    for c in expect_codes:
        if c not in codes:
            ok = False
            why.append("не поймано %s (есть %s)" % (c, sorted(codes)))
    return ok, "%-44s %s %s" % (name, "OK " if ok else "FAIL", "; ".join(why))


def selftest():
    """Режим --selftest: синтетический пример + мутации (изменённая строка, потерянная функция, дубль, #if, порядок…)."""
    results = []
    # 0. лексер: сырые строки/комментарии не ломают баланс
    try:
        m = FileModel("AsdaX.cpp", SELFTEST_BEFORE)
        names = [it.name for it in m.items if it.kind == "func"]
        need = ["Sum", "Twice", "NextSerial", "FAsdaX::FAsdaX", "UAsdaX::HandleA", "UAsdaX::HandleB", "Triple"]
        miss = [x for x in need if x not in names]
        kinds = {it.name: it.kind for it in m.items}
        ok = not m.errors and not miss and kinds.get("struct FAsdaXRow") == "type" and kinds.get("CVarAsdaA") == "object" \
            and any(it.kind == "macro_call" and it.name.startswith("IMPLEMENT_ASDA_THING") for it in m.items)
        cmd = [it for it in m.items if it.name == "CmdAsdaXDump"]
        ok = ok and cmd and cmd[0].cond == "ASDA_DEV_CONSOLE" and cmd[0].registration
        ns = [it for it in m.items if it.name == "Sum"]
        ok = ok and ns and ns[0].ns == "(anon)"
        ok = ok and [it for it in m.items if it.name == "NextSerial"][0].local_static
        results.append((ok, "%-44s %s %s" % ("разбор примера", "OK " if ok else "FAIL",
                                           "" if ok else "ошибки=%s нет=%s виды=%s" % (m.errors, miss, kinds))))
    except Exception as e:     # noqa: BLE001 — самотест обязан сообщить, а не упасть
        results.append((False, "разбор примера FAIL исключение %r" % e))
    good = [("AsdaX_Internal.h", SELFTEST_INTERNAL), ("AsdaX.cpp", SELFTEST_PART1), ("AsdaX_Net.cpp", SELFTEST_PART2)]
    # В правильном разрезании Twice переехал в заголовок; kRawUnused в заголовке — лишний объект → уберём его из «чистого» варианта.
    good_int = SELFTEST_INTERNAL.replace(
        "namespace\n{\n\t// Сырая строка с фигурными скобками и комментарием внутри.\n\tconst TCHAR* const kRawUnused = nullptr;\n}\n", "")
    good = [("AsdaX_Internal.h", good_int), ("AsdaX.cpp", SELFTEST_PART1), ("AsdaX_Net.cpp", SELFTEST_PART2)]
    results.append(_selftest_case("правильное разрезание = ЧИСТО", SELFTEST_BEFORE, good, [], forbid_errors=True))
    # 1. изменённая строка в теле
    bad = [(n, t.replace("if (Len < 4) { return; }", "if (Len < 8) { return; }")) for n, t in good]
    results.append(_selftest_case("изменённая строка в теле → CHANGED", SELFTEST_BEFORE, bad, ["CHANGED"]))
    # 2. потерянная функция
    bad = [(n, t.replace("// Метод мира.\nvoid UAsdaX::HandleB(int32 V) const\n{\n\tauto L = [this, V]() { return V + CVarAsdaA.GetValueOnGameThread(); };\n\t(void)L();\n}\n", "")) for n, t in good]
    results.append(_selftest_case("потерянная функция → MISSING", SELFTEST_BEFORE, bad, ["MISSING"]))
    # 3. дубль функции в двух частях
    bad = [good[0], good[1], (good[2][0], good[2][1] + "\n// Помощник со своим состоянием.\nstatic int32 NextSerial()\n{\n\tstatic int32 Serial = 0;\n\treturn ++Serial;\n}\n")]
    results.append(_selftest_case("дубль функции → DUPLICATE", SELFTEST_BEFORE, bad, ["DUPLICATE"]))
    # 4. команда вынесена из-под #if ASDA_DEV_CONSOLE
    bad = [(n, t.replace("#if ASDA_DEV_CONSOLE\n// Дев-команда.", "// Дев-команда.").replace(
        "Sum(1, 2)); }));\n#endif", "Sum(1, 2)); }));")) for n, t in good]
    results.append(_selftest_case("снят #if ASDA_DEV_CONSOLE → COND", SELFTEST_BEFORE, bad, ["COND"]))
    # 5. переставлены ключи A и B в одном файле
    p1 = SELFTEST_PART1.replace(
        "// Ключ A.\nTAutoConsoleVariable<int32> CVarAsdaA(TEXT(\"asda.X.A\"), 1, TEXT(\"ключ { A }\"));\n// Ключ B зависит от порядка — после A.\nstatic TAutoConsoleVariable<int32> CVarAsdaB(TEXT(\"asda.X.B\"), 0, TEXT(\"ключ B\"));",
        "// Ключ B зависит от порядка — после A.\nstatic TAutoConsoleVariable<int32> CVarAsdaB(TEXT(\"asda.X.B\"), 0, TEXT(\"ключ B\"));\n// Ключ A.\nTAutoConsoleVariable<int32> CVarAsdaA(TEXT(\"asda.X.A\"), 1, TEXT(\"ключ { A }\"));")
    bad = [good[0], ("AsdaX.cpp", p1), good[2]]
    results.append(_selftest_case("переставлены ключи → ORDER", SELFTEST_BEFORE, bad, ["ORDER"]))
    # 6. лишняя функция
    bad = [good[0], good[1], (good[2][0], good[2][1] + "\nstatic void NewHelper() {}\n")]
    results.append(_selftest_case("новая функция → EXTRA", SELFTEST_BEFORE, bad, ["EXTRA"]))
    # 7. функция перенесена в другое пространство имён
    bad = [(n, t.replace("namespace AsdaXNs\n", "namespace AsdaXOther\n")) for n, t in good]
    results.append(_selftest_case("сменилось пространство имён → NS", SELFTEST_BEFORE, bad, ["NS"]))
    # 8. static-функция с локальной static уехала в общий заголовок
    p1 = SELFTEST_PART1.replace("// Помощник со своим состоянием.\nstatic int32 NextSerial()\n{\n\tstatic int32 Serial = 0;\n\treturn ++Serial;\n}\n", "")
    bad = [("AsdaX_Internal.h", good_int + "\n// Помощник со своим состоянием.\nstatic int32 NextSerial()\n{\n\tstatic int32 Serial = 0;\n\treturn ++Serial;\n}\n"),
           ("AsdaX.cpp", p1), good[2]]
    results.append(_selftest_case("локальная static в заголовке → HEADER_LOCAL_STATIC", SELFTEST_BEFORE, bad, ["HEADER_LOCAL_STATIC"]))
    # 9. ключ-регистрация уехал в заголовок
    p1 = SELFTEST_PART1.replace("static TAutoConsoleVariable<int32> CVarAsdaB(TEXT(\"asda.X.B\"), 0, TEXT(\"ключ B\"));\n", "")
    bad = [("AsdaX_Internal.h", good_int + "static TAutoConsoleVariable<int32> CVarAsdaB(TEXT(\"asda.X.B\"), 0, TEXT(\"ключ B\"));\n"),
           ("AsdaX.cpp", p1), good[2]]
    results.append(_selftest_case("ключ в заголовке → HEADER_REG", SELFTEST_BEFORE, bad, ["HEADER_REG"]))
    # 10. изменение внутри сырой строки
    bad = [(n, t.replace('"a": "}"', '"a": "]"')) for n, t in good]
    results.append(_selftest_case("правка внутри сырой строки → CHANGED", SELFTEST_BEFORE, bad, ["CHANGED"]))
    # 11. пропал вызов макроса без ';'
    bad = [(n, t.replace("IMPLEMENT_ASDA_THING(UAsdaX)\n", "")) for n, t in good]
    results.append(_selftest_case("пропал макровызов → MISSING", SELFTEST_BEFORE, bad, ["MISSING"]))
    # 12. пробелы на концах строк и CRLF — не расхождение
    ws = [(n, t.replace("\n", "   \r\n")) for n, t in good]
    results.append(_selftest_case("CRLF и хвостовые пробелы = ЧИСТО", SELFTEST_BEFORE, ws, [], forbid_errors=True))
    # 13. правка внутри комментария в теле функции — тоже изменение текста
    bad = [(n, t.replace("// значение", "// значение!")) for n, t in good]
    results.append(_selftest_case("правка комментария в теле → CHANGED", SELFTEST_BEFORE, bad, ["CHANGED"]))
    # 14–16. разрезание switch по группам case (--explode-switch)
    sw_before = ("void UAsdaX::HandlePacket(const FPkt& PktIn)\n{\n\tconst FPkt& Pkt = PktIn;\n\tswitch (Pkt.Op)\n\t{\n"
                 "\tcase 1:        // вход\n\t{\n\t\tA = 1;\n\t\tbreak;\n\t}\n\tcase 2:\n\tcase 3:\n\t\tif (B) { return; }\n\t\tB = 2;\n\t\tbreak;\n"
                 "\tcase 4: { C = '}'; break; }\n\tdefault:\n\t\tbreak;\n\t}\n}\n")
    sw_a = ("void UAsdaX::HandlePacket_A(const FPkt& Pkt, bool& bHandled)\n{\n\tbHandled = true;\n\tswitch (Pkt.Op)\n\t{\n"
            "\tcase 1:        // вход\n\t{\n\t\tA = 1;\n\t\tbreak;\n\t}\n\tcase 2:\n\tcase 3:\n\t\tif (B) { return; }\n\t\tB = 2;\n\t\tbreak;\n"
            "\tdefault: bHandled = false; break;\n\t}\n}\n")
    sw_b = ("void UAsdaX::HandlePacket_B(const FPkt& Pkt, bool& bHandled)\n{\n\tbHandled = true;\n\tswitch (Pkt.Op)\n\t{\n"
            "\tcase 4: { C = '}'; break; }\n\tdefault: bHandled = false; break;\n\t}\n}\n")
    sw_disp = ("void UAsdaX::HandlePacket(const FPkt& PktIn)\n{\n\tconst FPkt& Pkt = PktIn;\n\tbool bHandled = false;\n"
               "\tHandlePacket_A(Pkt, bHandled); if (bHandled) { return; }\n\tHandlePacket_B(Pkt, bHandled); if (bHandled) { return; }\n"
               "\tswitch (Pkt.Op)\n\t{\n\tdefault:\n\t\tbreak;\n\t}\n}\n")

    def _sw_case(name, parts, expect, clean):
        """Случай самотеста режима --explode-switch."""
        bm = [FileModel("X.cpp", sw_before)]
        am = [FileModel(n, t) for n, t in parts]
        explode_switch(bm, "HandlePacket")
        explode_switch(am, "HandlePacket")
        rep = compare(bm, am)
        codes = {c for c, _ in rep.errors}
        ok = (not rep.errors) if clean else all(c in codes for c in expect)
        return ok, "%-44s %s %s" % (name, "OK " if ok else "FAIL", "" if ok else rep.errors[:3])

    try:
        g, _, _, _ = switch_cases(FileModel("X.cpp", sw_before), FileModel("X.cpp", sw_before).items[0])
        ok = [x.labels for x in g] == [["1"], ["2", "3"], ["4"], ["default"]] and g[1].has_return and not g[0].has_return
        results.append((ok, "%-44s %s %s" % ("switch: группы case и флаги", "OK " if ok else "FAIL",
                                           "" if ok else [x.labels for x in g])))
    except Exception as e:     # noqa: BLE001
        results.append((False, "switch: группы case FAIL исключение %r" % e))
    parts = [("X.cpp", sw_disp), ("X_A.cpp", sw_a), ("X_B.cpp", sw_b)]
    results.append(_sw_case("switch по областям = ЧИСТО", parts, [], True))
    results.append(_sw_case("switch: изменённый case → CHANGED", [(n, t.replace("B = 2;", "B = 3;")) for n, t in parts],
                            ["CHANGED"], False))
    results.append(_sw_case("switch: потерянный case → MISSING", [(n, t.replace("\tcase 4: { C = '}'; break; }\n", ""))
                                                                  for n, t in parts], ["MISSING"], False))
    # 20. прототип с функциональным типом в аргументе шаблона — объявление, а не объект (урок R3c-b 12.11.2026)
    try:
        fm = FileModel("P.cpp", "static void AsdaAfter(float Sec, TFunction<void()> Fn);\n"
                                "static const FName kX(TEXT(\"a\"));\nstatic TArray<int32> GArr(MakeArr());\n")
        kinds = [x.kind for x in fm.items]
        ok = kinds == ["decl", "object", "object"]
        results.append((ok, "%-44s %s %s" % ("прототип с TFunction<void()> → decl", "OK " if ok else "FAIL", "" if ok else kinds)))
    except Exception as e:     # noqa: BLE001
        results.append((False, "прототип с TFunction<void()> FAIL исключение %r" % e))
    # 21–24. строки препроцессора верхнего уровня (урок R3c-b 12.11.2026: К1 подмена, К2 потеря при склейке, К3 прилипание)
    pp_before = ("#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется\nstatic int GA = 1;\n#endif\n\n"
                 "#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда\nstatic int GB = 2;\n#endif\n\n"
                 "#if !UE_BUILD_SHIPPING\n// ★ самотест якорей — разбор у объявления.\nstatic int GC = 3;\n#endif\n")
    pp_good = pp_before.replace("\n#endif\n", "\n#endif // X\n")
    pp_k2 = ("#if ASDA_DEV_CONSOLE   // дев-команда, в Shipping не регистрируется\nstatic int GA = 1;\n\n"
             "static int GB = 2;\n#endif // ASDA_DEV_CONSOLE\n\n"
             "#if !UE_BUILD_SHIPPING\n// ★ самотест якорей — разбор у объявления.\nstatic int GC = 3;\n#endif\n")
    pp_k1 = pp_before.replace("// ★ античит А2: дев-команда\n", "// дев-команды нет\n")
    pp_k3 = pp_before.replace("#if !UE_BUILD_SHIPPING\n", "#if !UE_BUILD_SHIPPING   // ★ самотест якорей — разбор у объявления.\n")
    for nm, after, expect in (("pp: обёртки дословно = ЧИСТО", pp_good, None),
                              ("pp: склейка обёрток → PP_COMMENT_LOST", pp_k2, "PP_COMMENT_LOST"),
                              ("pp: подмена комментария → PP_COMMENT_LOST", pp_k1, "PP_COMMENT_LOST"),
                              ("pp: прилипший комментарий → FOREIGN", pp_k3, "PP_COMMENT_FOREIGN")):
        rep = compare([FileModel("P.cpp", pp_before)], [FileModel("P.cpp", after)], strict_comments=True)
        codes = {c for c, _ in rep.errors}
        ok = (expect in codes) if expect else not any(c.startswith("PP_") for c in codes)
        results.append((ok, "%-44s %s %s" % (nm, "OK " if ok else "FAIL", "" if ok else rep.errors[:3])))
    ok_all = all(ok for ok, _ in results)
    for _, line in results:
        print(line)
    print("SELFTEST: %s (%d/%d)" % ("PASS" if ok_all else "FAIL", sum(1 for ok, _ in results if ok), len(results)))
    return 0 if ok_all else 1


def main(argv=None):
    """Точка входа: разбор аргументов и выбор режима."""
    ap = argparse.ArgumentParser(description="Проверка дословности разрезания .cpp (рефакторинг R2).")
    ap.add_argument("--before", nargs="+", help="файл(ы) «до» (из бэкапа)")
    ap.add_argument("--after", nargs="+", help="файлы «после» (части + общий заголовок)")
    ap.add_argument("--report", help="записать отчёт Markdown в файл")
    ap.add_argument("--json", help="для --dump: записать список элементов в JSON")
    ap.add_argument("--dump", help="вывести определения верхнего уровня файла")
    ap.add_argument("--selftest", action="store_true", help="самотест на синтетическом примере")
    ap.add_argument("--allow-dup", nargs="*", default=[], help="имена, которым разрешено повторяться «после»")
    ap.add_argument("--strict-linkage", action="store_true", help="смена static/inline — ошибка, а не предупреждение")
    ap.add_argument("--strict-comments", action="store_true", help="пропажа комментария верхнего уровня — ошибка")
    ap.add_argument("--quiet", action="store_true", help="не печатать отчёт целиком, только итог")
    ap.add_argument("--explode-switch", metavar="ИМЯ",
                    help="функции с этим именем (и с этим префиксом «после», напр. HandlePacket_Login) сверять по группам case "
                         "их главного switch; пролог/эпилог выводятся для ручной сверки")
    ap.add_argument("--cases", metavar="ФУНКЦИЯ", help="с --dump: вывести группы case главного switch функции")
    a = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")   # консоль Windows (cp1251) не печатает «→»
    except (AttributeError, ValueError):
        pass
    if a.selftest:
        return selftest()
    try:
        if a.dump and a.cases:
            return dump_cases(a.dump, a.cases, a.json)
        if a.dump:
            m = dump(a.dump, a.json, quiet=a.quiet)
            return 2 if m.errors else 0
        if not a.before or not a.after:
            ap.error("нужны --before и --after (или --dump/--selftest)")
        bms, ams = load_models(a.before), load_models(a.after)
        hosts = []
        if a.explode_switch:
            hosts = explode_switch(bms, a.explode_switch) + explode_switch(ams, a.explode_switch)
        rep = compare(bms, ams, allow_dup=set(a.allow_dup),
                      strict_linkage=a.strict_linkage, strict_comments=a.strict_comments)
        if a.explode_switch:
            report_switch_hosts(rep, bms, hosts)
    except (LexError, ParseError, OSError) as e:
        print("ОШИБКА: %s" % e)
        return 2
    title = "%s → %s" % (", ".join(os.path.basename(x) for x in a.before), ", ".join(os.path.basename(x) for x in a.after))
    text = rep.render(title)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    if a.quiet:
        print(text.split("\n")[2])
    else:
        print(text)
    if any(c == "PARSE" for c, _ in rep.errors):
        return 2
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
