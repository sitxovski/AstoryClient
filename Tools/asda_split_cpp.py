# Copyright (c) 2026 @sitxovski (Discord)
# asda_split_cpp.py — РАЗНОС БОЛЬШОГО .cpp НА НЕСКОЛЬКО .cpp ТОГО ЖЕ КЛАССА С МАШИННЫМ ДОКАЗАТЕЛЬСТВОМ «НИ ОДНА СТРОКА НЕ ИЗМЕНЕНА»
#   (22.10.2026, слово владельца «огромные файлы разделить … с гарантией 100 %»; первая цель — Private/AsdaNetSubsystem.cpp).
#
# ЗАЧЕМ. Правка одного места в .cpp на 15 тыс. строк перекомпилирует весь файл (~100 с). Разнос по смысловым группам
#   сокращает цикл, но любой ручной перенос рискует потерять/задвоить строку или тихо сменить разрешение имён. Здесь
#   резка идёт ЦЕЛЫМИ строками исходника по плану (диапазоны строк → целевой файл), а `verify` доказывает побайтно, что
#   объединение результата = исходник (каждая строка ровно один раз, в исходном порядке), и ловит ловушки C++.
#
# КОМАНДЫ (обычный python, unreal не нужен):
#   python Tools/asda_split_cpp.py scan  <src.cpp> [--json <out.json>]   — сущности верхнего уровня, ловушки, файло-локальные символы
#   python Tools/asda_split_cpp.py split  --plan <plan.json> [--write]   — без --write только показать, что будет записано
#   python Tools/asda_split_cpp.py verify --plan <plan.json>             — доказательство (код 0 — расхождений 0)
#
# ПЛАН (JSON):
#   { "original": "<бэкап исходника — эталон для verify>", "source": "<живой путь исходника = ядро после разноса>",
#     "include_end": <последняя строка блока #include (включительно)>,
#     "core_note": ["строки комментария-шапки ядра", …],
#     "internal": { "file": "<путь Internal.h>", "note": [...], "items": [ {"range": [a, b], "mode": "inline"}, … ] },
#     "targets": [ { "file": "<путь .cpp>", "note": ["строки шапки", …], "ranges": [[a, b], …] }, … ] }
#   Строки 1..include_end — пролог: в каждый новый .cpp копируются ВСЕ его строки #include (байт в байт, тот же порядок),
#   прочие строки пролога (например, static-объявление между #include) остаются только в ядре. Строки, не попавшие ни в
#   один диапазон, остаются в ядре на своих местах. Единственное допустимое изменение текста — пункт `internal.items`
#   с mode "inline": сущность `static <…>` переезжает в закрытый заголовок как `inline <…>` (только слово `static`
#   первой значащей строки → `inline`; тело байт в байт) — для чистых функций без состояния, нужных нескольким файлам.
#
# ДОКАЗАТЕЛЬСТВО (`verify`):
#   1. Разбиение: диапазоны не пересекаются, лежат после пролога; каждая граница — на верхнем уровне (глубина {} = 0, вне
#      комментария/строки), блоки #if/#endif внутри диапазона сбалансированы и не начинаются снаружи.
#   2. Побайтно: каждый новый файл = BOM + шапка + #include пролога (+ #include Internal.h) + строки своих диапазонов подряд;
#      ядро = исходник без вынесенных строк (+ шапка и #include Internal.h после пролога); Internal.h = шапка + пункты
#      с заменой static→inline. Объединение тел = все строки исходника после пролога РОВНО по одному разу, порядок внутри
#      каждого файла = исходный.
#   3. Вторым методом: мультимножество сущностей верхнего уровня (определения функций/методов, данные, namespace-блоки),
#      извлечённых лексером из исходника и из всех файлов результата, совпадает посимвольно.
#   4. Ловушки: файло-локальные символы (static-функции/данные, const/constexpr, анонимные namespace, типы, псевдонимы,
#      макросы) — каждый определён ровно в одном файле (или в Internal.h) и упоминается только там, где виден; #define/#undef
#      и `using namespace` посреди файла, DEFINE_LOG_CATEGORY*, IMPLEMENT_*, глобальные объекты с конструкторами (порядок
#      инициализации между TU), шаблоны — перечисляются; запрещённое → код 1.
#
# ★ Модуль БЕЗ побочных эффектов при импорте (закон tools-no-import-side-effects): работа — только в main() под __main__.
import argparse
import collections
import io
import json
import os
import re
import sys

BOM = b"\xef\xbb\xbf"   # UTF-8 BOM — исходники проекта в UTF-8 с BOM

# Ключевые слова/служебные идентификаторы, не являющиеся именами символов (для таблицы ссылок)
KEYWORDS = set("""alignas alignof and asm auto bool break case catch char char8_t char16_t char32_t class const consteval constexpr
constinit const_cast continue decltype default delete do double dynamic_cast else enum explicit export extern false float for
friend goto if inline int long mutable namespace new noexcept not nullptr operator or private protected public register
reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch template this
thread_local throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while override final""".split())


# ───────────────────────────────────────────────────────────── лексер
class Tok(object):
    """Значащий токен: вид (id/num/str/chr/punct), значение, номер строки (с 1)."""
    __slots__ = ("kind", "val", "line")

    def __init__(self, kind, val, line):
        self.kind, self.val, self.line = kind, val, line


class Lexed(object):
    """Итог лексера: токены, директивы препроцессора, состояние в начале каждой строки."""

    def __init__(self):
        self.toks = []          # значащие токены (без комментариев и директив)
        self.pp = []            # (строка, текст директивы, глубина {} в момент директивы)
        self.line_state = {}    # строка → (глубина {}, состояние: 'code'|'block'|'raw'|'pp', глубина #if-стека)
        self.pp_depth_at = {}   # строка директивы → глубина #if-стека ДО неё


def lex(text):
    """Лексер C++ уровня, достаточного для границ блоков: комментарии, строки (вкл. raw и префиксы), символьные литералы,
    числа с разделителем ', директивы препроцессора (с продолжением строки). text — без BOM, переводы строк '\\n'."""
    L = Lexed()
    n = len(text)
    i, line = 0, 1
    depth = 0
    pp_stack = 0
    at_line_start = True     # с начала строки были только пробелы
    state_at_start = "code"
    L.line_state[1] = (0, "code", 0)

    def newline(st):
        nonlocal line
        line += 1
        L.line_state[line] = (depth, st, pp_stack)

    while i < n:
        c = text[i]
        if c == "\n":
            newline("code")
            at_line_start = True
            i += 1
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        # директива препроцессора
        if c == "#" and at_line_start:
            start_line = line
            j = i
            buf = []
            while j < n:
                ch = text[j]
                if ch == "\n":
                    # продолжение строки обратной косой
                    k = len(buf) - 1
                    while k >= 0 and buf[k] in " \t\r":
                        k -= 1
                    if k >= 0 and buf[k] == "\\":
                        buf.append(ch)
                        j += 1
                        line += 1
                        L.line_state[line] = (depth, "pp", pp_stack)
                        continue
                    break
                if ch == "/" and j + 1 < n and text[j + 1] == "*":
                    # блочный комментарий внутри директивы
                    e = text.find("*/", j + 2)
                    if e < 0:
                        e = n
                    seg = text[j:e + 2]
                    for _ in range(seg.count("\n")):
                        line += 1
                        L.line_state[line] = (depth, "block", pp_stack)
                    buf.append(" ")
                    j = e + 2
                    continue
                if ch == "/" and j + 1 < n and text[j + 1] == "/":
                    # строчный комментарий до конца строки
                    while j < n and text[j] != "\n":
                        j += 1
                    continue
                buf.append(ch)
                j += 1
            d = "".join(buf).strip()
            m = re.match(r"#\s*(\w+)", d)
            kw = m.group(1) if m else ""
            L.pp_depth_at[start_line] = pp_stack
            L.pp.append((start_line, d, depth))
            if kw in ("if", "ifdef", "ifndef"):
                pp_stack += 1
            elif kw == "endif":
                pp_stack -= 1
            i = j
            continue
        at_line_start = False
        # комментарии
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            e = text.find("*/", i + 2)
            if e < 0:
                raise ValueError("незакрытый /* на строке %d" % line)
            seg = text[i:e + 2]
            for _ in range(seg.count("\n")):
                newline("block")
            i = e + 2
            continue
        # идентификатор (и префиксы строк)
        if c.isalpha() or c == "_" or ord(c) > 127:
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == "_" or ord(text[j]) > 127):
                j += 1
            ident = text[i:j]
            if j < n and text[j] == '"' and ident in ("R", "LR", "uR", "UR", "u8R"):
                m = re.match(r'"([^()\\ \t\n]{0,16})\(', text[j:])
                if not m:
                    raise ValueError("raw-строка без разделителя на строке %d" % line)
                delim = ")" + m.group(1) + '"'
                e = text.find(delim, j + len(m.group(0)))
                if e < 0:
                    raise ValueError("незакрытая raw-строка на строке %d" % line)
                seg = text[i:e + len(delim)]
                L.toks.append(Tok("str", seg, line))
                for _ in range(seg.count("\n")):
                    newline("raw")
                i = e + len(delim)
                continue
            if j < n and text[j] in "\"'" and ident in ("L", "u", "U", "u8"):
                i = j   # префикс — часть литерала; литерал разберёт ветка ниже
                continue
            L.toks.append(Tok("id", ident, line))
            i = j
            continue
        # число (с разделителем ')
        if c.isdigit() or (c == "." and i + 1 < n and text[i + 1].isdigit()):
            j = i + 1
            while j < n:
                ch = text[j]
                if ch.isalnum() or ch in "._":
                    j += 1
                elif ch == "'" and j + 1 < n and text[j + 1].isalnum():
                    j += 1
                elif ch in "+-" and text[j - 1] in "eEpP" and not text[i:j].lower().startswith("0x"):
                    j += 1
                elif ch in "+-" and text[j - 1] in "pP":
                    j += 1
                else:
                    break
            L.toks.append(Tok("num", text[i:j], line))
            i = j
            continue
        # строка / символ
        if c in "\"'":
            q = c
            j = i + 1
            while j < n and text[j] != q:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "\n":
                    raise ValueError("перевод строки внутри литерала на строке %d" % line)
                j += 1
            L.toks.append(Tok("str" if q == '"' else "chr", text[i:j + 1], line))
            i = j + 1
            continue
        # пунктуация
        two = text[i:i + 2]
        if two in ("::", "->"):
            L.toks.append(Tok("punct", two, line))
            i += 2
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth < 0:
                raise ValueError("лишняя } на строке %d" % line)
        L.toks.append(Tok("punct", c, line))
        i += 1
    L.final_depth = depth
    L.final_pp = pp_stack
    L.nlines = line
    return L


# ───────────────────────────────────────────────────────────── сущности верхнего уровня
class Entity(object):
    """Сущность верхнего уровня: строки [first, last] (по токенам), вид, имя, голова (токены до тела)."""

    def __init__(self, toks):
        self.toks = toks
        self.first = toks[0].line
        self.last = toks[-1].line
        self.kind, self.name, self.local_names = classify(toks)
        # ссылки на свободные имена: идентификатор не после `::`, `.`, `->` (квалифицированные имена и члены — не файло-локальные)
        self.refs = set(t.val for k, t in enumerate(toks)
                        if t.kind == "id" and not (k > 0 and toks[k - 1].kind == "punct" and toks[k - 1].val in ("::", ".", "->")))


def split_entities(L):
    """Разбить токены на сущности верхнего уровня (по ; и закрывающей } на глубине 0)."""
    ents = []
    cur = []
    depth = 0
    paren = 0
    toks = L.toks
    k = 0
    while k < len(toks):
        t = toks[k]
        cur.append(t)
        if t.kind == "punct":
            if t.val == "(":
                paren += 1
            elif t.val == ")":
                paren -= 1
            elif t.val == "{":
                depth += 1
            elif t.val == "}":
                depth -= 1
                if depth == 0 and paren == 0:
                    nxt = toks[k + 1] if k + 1 < len(toks) else None
                    if nxt is not None and nxt.kind == "punct" and nxt.val == ";":
                        cur.append(nxt)
                        k += 2
                        ents.append(Entity(cur))
                        cur = []
                        continue
                    if nxt is not None and nxt.kind == "punct" and nxt.val in (",", "{", ")", ".", "->", "(", "[", "?", ":", "+", "-", "*", "&", "|", "<", ">", "="):
                        k += 1
                        continue
                    head = head_tokens(cur)
                    hv = [h.val for h in head]
                    needs_semi = (any(v in ("struct", "class", "union", "enum", "typedef") for v in hv[:3]) or "=" in top_vals(head)) and "namespace" not in hv[:2]
                    if not needs_semi:
                        ents.append(Entity(cur))
                        cur = []
            elif t.val == ";" and depth == 0 and paren == 0:
                ents.append(Entity(cur))
                cur = []
        k += 1
    if cur:
        raise ValueError("незавершённая сущность с строки %d" % cur[0].line)
    return ents


def top_vals(head):
    """Значения токенов головы вне круглых скобок (аргументы по умолчанию `= nullptr` внутри (…) не считаются)."""
    out = []
    d = 0
    for t in head:
        if t.val == "(":
            d += 1
        elif t.val == ")":
            d -= 1
        elif d == 0:
            out.append(t.val)
    return out


def head_tokens(toks):
    """Токены головы сущности: до первой { на глубине 0 (или все, если тела нет)."""
    out = []
    for t in toks:
        if t.kind == "punct" and t.val == "{":
            break
        out.append(t)
    return out


def classify(toks):
    """(вид, имя, файло-локальные имена) сущности. Виды: method, func, static_func, static_func_decl, static_data, const_data,
    global_data, namespace, anon_namespace, type, alias, template, macro_stmt, other."""
    head = head_tokens(toks)
    hv = [t.val for t in head]
    body_is_brace = len(head) < len(toks)
    # первая { внутри незакрытых скобок — это лямбда/список в аргументах конструктора объекта, а не тело функции
    if body_is_brace and hv.count("(") > hv.count(")"):
        body_is_brace = False
        p = hv.index("(")
        nm = macro_obj_name(head) if hv and hv[0] == "static" else ""
        nm = nm or (head[p - 1].val if p >= 1 and head[p - 1].kind == "id" else "")
        if hv and hv[0] == "static":
            return "static_data", nm, set([nm])
        return "global_data", nm, set()
    first = hv[0] if hv else ""
    local = set()
    if first == "namespace":
        if len(hv) >= 2 and head[1].kind == "id":
            # именованный namespace, открытый в .cpp: его имя файло-локально, пока не доказано обратное (заголовок)
            return "namespace", hv[1], set([hv[1]])
        # анонимный: все имена, объявленные внутри (грубо — идентификаторы перед '(' / '=' / ';' на глубине 1)
        return "anon_namespace", "", inner_decl_names(toks)
    if first == "template":
        nm = name_before_paren(head)
        return "template", nm, set([nm]) if nm else local
    if first in ("using", "typedef"):
        # using X = ...; / typedef ... X;  / using namespace ...;
        if first == "using" and len(hv) > 1 and hv[1] == "namespace":
            return "using_namespace", hv[2] if len(hv) > 2 else "", local
        if first == "using" and len(hv) > 2 and hv[2] == "=":
            return "alias", hv[1], set([hv[1]])
        ids = [t.val for t in head if t.kind == "id"]
        nm = ids[-1] if ids else ""
        return "alias", nm, set([nm]) if nm else local
    lead = [v for v in hv[:4]]
    if first in ("struct", "class", "union", "enum") and body_is_brace:
        k = 1
        while k < len(hv) and hv[k] in ("class", "struct"):
            k += 1
        nm = hv[k] if k < len(hv) else ""
        names = set([nm]) if nm and head[k].kind == "id" else set()
        if first == "enum" and not (len(hv) > 1 and hv[1] in ("class", "struct")):
            names |= enum_names(toks)   # перечислители видны снаружи только у НЕограниченного enum
        return "type", nm, names
    if "::" in hv:
        # определение метода/статического члена класса: Класс::Имя( или Тип Класс::Имя = ...
        idx = hv.index("::")
        # последнее Класс::Имя перед '('
        p = hv.index("(") if "(" in hv else len(hv)
        qual = [i for i in range(min(p, len(hv))) if hv[i] == "::"]
        if qual:
            q = qual[-1]
            cls = hv[q - 1] if q >= 1 else ""
            nm = hv[q + 1] if q + 1 < len(hv) else ""
            if nm == "~" and q + 2 < len(hv):
                nm = "~" + hv[q + 2]
            if cls.startswith(("U", "A", "F", "S")) and cls[1:2].isupper() or cls == nm:
                if "(" in hv and body_is_brace:
                    return "method", cls + "::" + nm, local
                if "(" in hv:
                    return "method_decl_or_data", cls + "::" + nm, local
                return "member_data", cls + "::" + nm, local
    is_static = "static" in lead
    tv = top_vals(head)
    is_const = ("const" in lead or "constexpr" in lead) and "(" not in tv[:tv.index("=")] if "=" in tv else ("constexpr" in lead)
    if is_static:
        nm = name_before_paren(head)
        if body_is_brace and "(" in hv and "=" not in hv[:hv.index("(")]:
            return "static_func", nm, set([nm])
        if "(" in hv and not body_is_brace and "=" not in hv[:hv.index("(")]:
            args = head[hv.index("(") + 1:]
            # объявление функции: в скобках есть «Тип Имя» (два идентификатора подряд) или аргумент по умолчанию `= …`
            typed = any(args[k].kind == "id" and args[k + 1].kind == "id" for k in range(len(args) - 1)) or "=" in [a.val for a in args]
            is_macro_obj = head[0].val == "static" and len(head) > 1 and head[1].val.startswith("ASDA_")
            if is_macro_obj or (not typed and any(a.kind in ("str", "num", "chr") for a in args)):
                nm = macro_obj_name(head) or nm
                return "static_data", nm, set([nm])
            return "static_func_decl", nm, set([nm])
        nm = data_name(head)
        if hv and hv[-1] in ("struct", "class", "union") and body_is_brace:
            # `static const struct { … } Имя[] = { … };` — имя после закрывающей } безымянного типа
            d = 0
            for k in range(len(head), len(toks)):
                if toks[k].val == "{":
                    d += 1
                elif toks[k].val == "}":
                    d -= 1
                    if d == 0:
                        nxt = [t for t in toks[k + 1:] if t.kind == "id"]
                        nm = nxt[0].val if nxt else nm
                        break
        return "static_data", nm, set([nm])
    if is_const and "(" not in hv[:2]:
        nm = data_name(head)
        return "const_data", nm, set([nm])
    if head and head[0].kind == "id" and re.match(r"^[A-Z_][A-Z0-9_]*$", head[0].val) and len(hv) > 2 and hv[1] == "(":
        nm = head[2].val if head[2].kind == "id" else ""   # DEFINE_LOG_CATEGORY_STATIC(Имя, …) — первый аргумент
        if hv[0].startswith("ASDA_DEV_CVAR"):
            nm = macro_obj_name(head)                       # ASDA_DEV_CVAR(Тип, Имя, …) без static (после разноса — extern)
        return "macro_stmt", hv[0] + (":" + nm if nm else ""), set([nm]) if nm else local
    if "(" in hv and body_is_brace and "=" not in hv[:hv.index("(")]:
        return "func", name_before_paren(head), local
    nm = data_name(head)
    return "global_data", nm, local


def name_before_paren(head):
    """Идентификатор перед первой '(' на глубине 0 головы."""
    for k, t in enumerate(head):
        if t.kind == "punct" and t.val == "(":
            for b in range(k - 1, -1, -1):
                if head[b].kind == "id":
                    return head[b].val
            return ""
    return ""


def macro_obj_name(head):
    """Имя объекта в макро-объявлении `static ASDA_DEV_CVAR(Type, Name, …)` / `TAutoConsoleVariable<T> Name(…)`."""
    hv = [t.val for t in head]
    for k, t in enumerate(head):
        if t.kind == "id" and t.val.startswith("ASDA_") and k + 1 < len(head) and head[k + 1].val == "(":
            # второй аргумент
            args = []
            d = 0
            for u in head[k + 2:]:
                if u.val in ("(", "<", "["):
                    d += 1
                elif u.val in (")", ">", "]"):
                    d -= 1
                if d < 0:
                    break
                args.append(u)
            commas = [i for i, u in enumerate(args) if u.val == ","]
            if t.val.endswith("_REF"):
                # ASDA_DEV_CVAR_REF(Имя, TEXT(…), Переменная, …) — имя объекта = первый аргумент
                seg = args[:commas[0]] if commas else args
                ids = [u.val for u in seg if u.kind == "id"]
                return ids[-1] if ids else ""
            if len(commas) >= 1:
                seg = args[commas[0] + 1:commas[1] if len(commas) > 1 else len(args)]
                ids = [u.val for u in seg if u.kind == "id"]
                return ids[-1] if ids else ""
    return name_before_paren(head)


def data_name(head):
    """Имя переменной в объявлении данных: идентификатор перед '=', '[', '(' или концом."""
    for k, t in enumerate(head):
        if t.kind == "punct" and t.val in ("=", "[", "(", ";", "{"):
            for b in range(k - 1, -1, -1):
                if head[b].kind == "id":
                    return head[b].val
            return ""
    ids = [t.val for t in head if t.kind == "id"]
    return ids[-1] if ids else ""


def enum_names(toks):
    """Имена перечислителей enum."""
    out = set()
    inside = False
    expect = True
    for t in toks:
        if t.val == "{":
            inside = True
            expect = True
            continue
        if not inside:
            continue
        if t.val == "}":
            break
        if expect and t.kind == "id":
            out.add(t.val)
            expect = False
        elif t.val == ",":
            expect = True
    return out


def inner_decl_names(toks):
    """Имена, объявленные на глубине 1 анонимного namespace (функции, данные, типы)."""
    out = set()
    depth = 0
    stmt = []
    for t in toks:
        if t.val == "{":
            depth += 1
            if depth == 2:
                h = stmt
                nm = name_before_paren(h) if any(x.val == "(" for x in h) else data_name(h)
                if h and h[0].val in ("struct", "class", "enum"):
                    nm = h[1].val if len(h) > 1 else nm
                if nm:
                    out.add(nm)
                stmt = []
            continue
        if t.val == "}":
            depth -= 1
            continue
        if depth == 1:
            if t.val == ";":
                if stmt:
                    nm = data_name(stmt) if not any(x.val == "(" for x in stmt) else name_before_paren(stmt)
                    if nm:
                        out.add(nm)
                stmt = []
            else:
                stmt.append(t)
    return out


# ───────────────────────────────────────────────────────────── файлы
def read_lines(path):
    """Байтовые строки файла (с окончаниями) и признак BOM."""
    data = open(path, "rb").read()
    bom = data.startswith(BOM)
    if bom:
        data = data[len(BOM):]
    lines = data.splitlines(True)
    return lines, bom


def text_of(lines):
    """Текст для лексера: UTF-8, окончания строк → '\\n'."""
    return b"".join(lines).decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")


def analyse(lines):
    """Лексер + сущности по байтовым строкам."""
    L = lex(text_of(lines))
    if L.final_depth != 0 or L.final_pp != 0:
        raise ValueError("несбалансированный файл: {} %d, #if %d" % (L.final_depth, L.final_pp))
    return L, split_entities(L)


TRAP_PP = re.compile(r"#\s*(define|undef)\b")
TRAP_LOG = re.compile(r"\b(DEFINE_LOG_CATEGORY\w*|IMPLEMENT_\w+)\s*\(")
GLOBAL_CTOR_TYPES = re.compile(r"\b(TAutoConsoleVariable|FAutoConsoleCommand\w*|FAutoConsoleVariable\w*|ASDA_DEV_CVAR\w*|ASDA_DEV_CONSOLE_\w*|FAsdaDev\w*|TAsdaConstCVar)\b")


def scan_report(lines, L, ents, include_end=0):
    """Список ловушек и файло-локальных символов (словарь)."""
    rep = collections.OrderedDict()
    rep["entities"] = [{"first": e.first, "last": e.last, "kind": e.kind, "name": e.name} for e in ents]
    traps = []
    for (ln, d, dep) in L.pp:
        if TRAP_PP.match(d) and ln > include_end:
            traps.append({"line": ln, "trap": "pp_define_undef_mid_file", "text": d})
        if re.match(r"#\s*pragma\b", d):
            traps.append({"line": ln, "trap": "pragma", "text": d})
    for e in ents:
        txt = " ".join(t.val for t in head_tokens(e.toks))
        if e.kind == "using_namespace":
            traps.append({"line": e.first, "trap": "using_namespace", "text": txt})
        if e.kind in ("anon_namespace",):
            traps.append({"line": e.first, "trap": "anon_namespace", "text": ",".join(sorted(e.local_names))})
        if e.kind == "template":
            traps.append({"line": e.first, "trap": "template", "text": e.name})
        if TRAP_LOG.search(txt):
            traps.append({"line": e.first, "trap": "log_or_implement", "text": txt[:120]})
        if e.kind in ("static_data", "global_data", "const_data", "macro_stmt") and (GLOBAL_CTOR_TYPES.search(txt) or e.kind in ("global_data", "macro_stmt")):
            traps.append({"line": e.first, "trap": "global_object_ctor", "text": "%s %s" % (e.kind, e.name)})
    rep["traps"] = traps
    locals_ = collections.OrderedDict()
    for e in ents:
        for nm in e.local_names:
            if nm:
                locals_.setdefault(nm, []).append({"first": e.first, "kind": e.kind})
    rep["file_locals"] = locals_
    return rep


def enclosing_users(ents, names):
    """Для каждого файло-локального имени — список сущностей (first,kind,name), которые на него ссылаются."""
    out = {}
    for nm in names:
        out[nm] = [(e.first, e.kind, e.name) for e in ents if nm in e.refs and nm not in e.local_names]
    return out


def cmd_scan(a):
    """Команда scan."""
    lines, _bom = read_lines(a.src)
    L, ents = analyse(lines)
    rep = scan_report(lines, L, ents, a.include_end)
    users = enclosing_users(ents, list(rep["file_locals"].keys()))
    rep["users"] = {k: v for k, v in users.items()}
    kinds = collections.Counter(e.kind for e in ents)
    print("строк %d, сущностей %d: %s" % (len(lines), len(ents), ", ".join("%s=%d" % kv for kv in sorted(kinds.items()))))
    for t in rep["traps"]:
        print("ЛОВУШКА %-26s стр.%-6d %s" % (t["trap"], t["line"], t["text"]))
    for nm, defs in rep["file_locals"].items():
        print("ЛОКАЛЬНЫЙ %-40s опр.%s пользователей %d" % (nm, ",".join("%d(%s)" % (d["first"], d["kind"]) for d in defs), len(users.get(nm, []))))
    if a.json:
        with io.open(a.json, "w", encoding="utf-8") as f:
            json.dump(rep, f, ensure_ascii=False, indent=1)
    return 0


# ───────────────────────────────────────────────────────────── план → файлы
def load_plan(path):
    """Прочитать план разбиения (JSON, UTF-8)."""
    with io.open(path, encoding="utf-8") as f:
        return json.load(f)


def eol_of(lines):
    """Окончание строк исходника (CRLF/LF) — для сгенерированных строк."""
    return b"\r\n" if lines and lines[0].endswith(b"\r\n") else b"\n"


def gen_lines(texts, eol):
    """Сгенерированные строки (как есть) с окончанием исходника."""
    return [t.encode("utf-8") + eol for t in texts]


def note_lines(note, eol):
    """Сгенерированные строки шапки: каждая строка — комментарий `// …`."""
    return gen_lines([("// " + s) if s else "//" for s in note], eol)


def include_lines(lines, include_end):
    """Все строки #include пролога (байт в байт, в исходном порядке) и обёртки условной компиляции вокруг них
    (#if WITH_EDITOR … #endif) — иначе редакторские заголовки попали бы в игровую сборку без условия."""
    return [ln for ln in lines[:include_end] if re.match(rb"\s*#\s*(include|if|ifdef|ifndef|elif|else|endif)\b", ln)]


def internal_include_line(plan, eol):
    """Строка #include закрытого заголовка (или None, если его нет)."""
    it = plan.get("internal")
    if not it:
        return None
    name = os.path.basename(it["file"])
    cm = it.get("include_comment", "★ 22.10.2026 разнос: общее для частей AsdaNetSubsystem*.cpp (LogAsdaNet, чистые хелперы)")
    return ('#include "%s"   // %s' % (name, cm)).encode("utf-8") + eol


def static_to_inline(seg):
    """Пункт Internal.h mode=inline: в первой значащей строке (не комментарий/не пустая) `static ` → `inline `."""
    out = list(seg)
    for k, ln in enumerate(out):
        s = ln.lstrip()
        if not s.strip() or s.startswith(b"//"):
            continue
        m = re.match(rb"(\s*)static ", ln)
        if not m:
            raise ValueError("пункт Internal.h: первая значащая строка не начинается со `static `: %r" % ln[:80])
        out[k] = m.group(1) + b"inline " + ln[m.end():]
        return out
    raise ValueError("пункт Internal.h без значащей строки")


def item_text(lines, it):
    """Строки пункта Internal.h после допустимого преобразования режима."""
    a, b = it["range"]
    seg = lines[a - 1:b]
    mode = it.get("mode")
    if mode == "keep":
        return list(seg)
    if mode == "inline":
        return static_to_inline(seg)
    raise ValueError("пункт Internal.h %s: неизвестный режим %r" % (it["range"], mode))


def replaced_line(lines, rp, eol):
    """Строка ядра для пункта `replace`: исходная обязана совпасть с `old` (без окончания) — иначе ошибка."""
    ln = lines[rp["line"] - 1]
    body = ln.rstrip(b"\r\n")
    if body.decode("utf-8") != rp["old"]:
        raise ValueError("replace стр.%d: в исходнике %r, а план ждёт %r" % (rp["line"], body[:80], rp["old"]))
    return rp["new"].encode("utf-8") + ln[len(body):]


class Built(object):
    """Ожидаемый результат: файлы (байты), их сгенерированные головы, раскладка диапазонов, строки исходника."""

    def __init__(self):
        self.outs = collections.OrderedDict()    # путь → полный ожидаемый файл (bytes)
        self.heads = {}                          # путь → ожидаемая сгенерированная голова (bytes, включая BOM)
        self.layout = collections.OrderedDict()  # путь → [(a, b)] диапазоны строк исходника в теле файла
        self.lines = []
        self.kind = {}                           # путь → 'core' | 'target' | 'internal'


def build_outputs(plan):
    """Ожидаемое содержимое всех файлов результата (детерминированно из плана и бэкапа исходника)."""
    R = Built()
    lines, bom = read_lines(plan["original"])
    R.lines = lines
    eol = eol_of(lines)
    inc_end = plan["include_end"]
    incs = include_lines(lines, inc_end)
    iline = internal_include_line(plan, eol)
    internal = plan.get("internal") or {}
    taken = {}

    def take(rng, owner):
        a, b = rng
        if a <= inc_end or b < a or b > len(lines):
            raise ValueError("диапазон %s: вне тела исходника (пролог 1..%d, строк %d)" % (rng, inc_end, len(lines)))
        for x in range(a, b + 1):
            if x in taken:
                raise ValueError("строка %d в двух местах: %s и %s" % (x, taken[x], owner))
            taken[x] = owner

    for t in plan["targets"]:
        for r in t["ranges"]:
            take(r, t["file"])
    for it in internal.get("items", []):
        take(it["range"], internal["file"])
    repl = {}
    for rp in plan.get("replace", []):
        if rp["line"] in taken:
            raise ValueError("replace стр.%d: строка обязана остаться в ядре" % rp["line"])
        repl[rp["line"]] = replaced_line(lines, rp, eol)
    # новые .cpp
    for t in plan["targets"]:
        rngs = sorted(tuple(r) for r in t["ranges"])
        head = BOM + b"".join(note_lines(t["note"], eol) + incs + ([iline] if iline else []) + [eol])
        body = b"".join(b"".join(lines[a - 1:b]) for (a, b) in rngs)
        R.outs[t["file"]] = head + body
        R.heads[t["file"]] = head
        R.layout[t["file"]] = rngs
        R.kind[t["file"]] = "target"
    # Internal.h
    if internal:
        items = sorted(internal.get("items", []), key=lambda x: x["range"][0])
        head = BOM + b"".join(note_lines(internal["note"], eol) + [b"#pragma once" + eol, eol]
                              + gen_lines(internal.get("includes", []), eol) + [eol]
                              + gen_lines(internal.get("decls", []), eol) + [eol])
        body = b"".join(b"".join(item_text(lines, it)) for it in items)
        R.outs[internal["file"]] = head + body
        R.heads[internal["file"]] = head
        R.layout[internal["file"]] = [tuple(it["range"]) for it in items]
        R.kind[internal["file"]] = "internal"
    # ядро: шапка-комментарий сверху, пролог байт в байт, #include Internal.h, остальное — невынесенные строки
    # пролог ядра байт в байт (кроме строк `replace`), #include Internal.h — после строки internal_include_after
    #   (по умолчанию — после пролога; раньше, если в прологе стоит определение, которому нужно объявление из Internal.h)
    pro = [repl.get(x, lines[x - 1]) for x in range(1, inc_end + 1)]
    ia = plan.get("internal_include_after", inc_end)
    if not (0 <= ia <= inc_end):
        raise ValueError("internal_include_after %d вне пролога 0..%d" % (ia, inc_end))
    head = (BOM if bom else b"") + b"".join(note_lines(plan.get("core_note", []), eol)) + b"".join(pro[:ia]) + (iline or b"") + b"".join(pro[ia:])
    core_body = []
    core_ranges = []
    run_a = None
    for x in range(inc_end + 1, len(lines) + 1):
        if x not in taken:
            core_body.append(repl.get(x, lines[x - 1]))
            if run_a is None:
                run_a = x
        elif run_a is not None:
            core_ranges.append((run_a, x - 1))
            run_a = None
    if run_a is not None:
        core_ranges.append((run_a, len(lines)))
    R.outs[plan["source"]] = head + b"".join(core_body)
    R.heads[plan["source"]] = head
    R.layout[plan["source"]] = core_ranges
    R.kind[plan["source"]] = "core"
    return R


def cmd_split(a):
    """Команда split: записать (с --write) или показать файлы результата."""
    plan = load_plan(a.plan)
    if os.path.abspath(plan["original"]) == os.path.abspath(plan["source"]):
        raise SystemExit("original и source совпадают — original обязан быть бэкапом")
    if open(plan["original"], "rb").read() != open(plan["source"], "rb").read() and not a.force:
        raise SystemExit("живой исходник отличается от бэкапа original — разнос только с нетронутого файла (--force для повтора)")
    errs = check_cuts(plan)
    if errs:
        for e in errs:
            print("ОШИБКА " + e)
        return 1
    R = build_outputs(plan)
    for p, data in R.outs.items():
        print("%s %-90s строк %6d" % ("ЗАПИСЬ" if a.write else "будет", p, data.count(b"\n")))
    if a.write:
        for p in R.outs:
            if p != plan["source"] and os.path.exists(p) and not a.force:
                raise SystemExit("файл уже существует: %s" % p)
        for p, data in R.outs.items():
            with open(p, "wb") as f:
                f.write(data)
    return 0


# ───────────────────────────────────────────────────────────── проверки
def check_cuts(plan):
    """Каждая граница диапазона — на верхнем уровне (глубина {} 0, вне комментария/строки, #if-стек пуст);
    #if внутри диапазона сбалансирован; строки вне сущностей у границ — только пустые/комментарии/директивы."""
    lines, _ = read_lines(plan["original"])
    L = lex(text_of(lines))
    errs = []
    rngs = [(tuple(r), t["file"]) for t in plan["targets"] for r in t["ranges"]]
    rngs += [(tuple(it["range"]), plan["internal"]["file"]) for it in (plan.get("internal") or {}).get("items", [])]
    for (a, b), f in rngs:
        for x in (a, b + 1):
            st = L.line_state.get(x, (0, "code", 0))
            if st[0] != 0 or st[1] != "code" or st[2] != 0:
                errs.append("граница %d (диапазон %d-%d → %s): глубина {}=%d, состояние %s, #if=%d" % (x, a, b, os.path.basename(f), st[0], st[1], st[2]))
        lvl = 0
        for (ln, d, dep) in L.pp:
            if a <= ln <= b:
                kw = re.match(r"#\s*(\w+)", d).group(1)
                if kw in ("if", "ifdef", "ifndef"):
                    lvl += 1
                elif kw == "endif":
                    lvl -= 1
                elif kw in ("else", "elif") and lvl == 0:
                    errs.append("диапазон %d-%d: #%s без своего #if (стр.%d)" % (a, b, kw, ln))
                if lvl < 0:
                    errs.append("диапазон %d-%d: #endif без #if (стр.%d)" % (a, b, ln))
                    lvl = 0
        if lvl != 0:
            errs.append("диапазон %d-%d: #if не закрыт внутри диапазона" % (a, b))
    # токен не должен пересекать границу: ни один токен (включая многострочные) не лежит на строках по обе стороны
    bounds = set()
    for (a, b), f in rngs:
        bounds.add(a)
        bounds.add(b + 1)
    ents = split_entities(L)
    for e in ents:
        for x in bounds:
            if e.first < x <= e.last:
                errs.append("граница %d режет сущность %s (%d-%d)" % (x, e.name, e.first, e.last))
    return errs


def entity_counter(lines):
    """Мультимножество сущностей верхнего уровня: текст = строки сущности целиком (байт в байт); и сами сущности."""
    L, ents = analyse(lines)
    c = collections.Counter()
    for e in ents:
        c[b"".join(lines[e.first - 1:e.last])] += 1
    return c, ents, L


def cmd_verify(a):
    """Команда verify: доказательство неизменности + ловушки. Код 0 — 0 расхождений и 0 запретов."""
    plan = load_plan(a.plan)
    problems = []
    problems += check_cuts(plan)
    R = build_outputs(plan)
    lines = R.lines
    inc_end = plan["include_end"]
    eol = eol_of(lines)
    disk = {}
    for p in R.outs:
        if not os.path.isfile(p):
            problems.append("нет файла %s" % p)
            continue
        disk[p] = open(p, "rb").read()
    # (1) побайтно: файл на диске == построенный по плану из бэкапа
    for p, data in R.outs.items():
        if p in disk and disk[p] != data:
            dl, el = disk[p].splitlines(True), data.splitlines(True)
            k = next((i for i in range(min(len(dl), len(el))) if dl[i] != el[i]), min(len(dl), len(el)))
            problems.append("%s: отличается от построенного по плану, первая строка %d" % (os.path.basename(p), k + 1))
    # (2) разбиение: каждая строка тела исходника ровно в одном файле
    seen = collections.Counter()
    for p, rngs in R.layout.items():
        for (a_, b_) in rngs:
            for x in range(a_, b_ + 1):
                seen[x] += 1
    missing = [x for x in range(inc_end + 1, len(lines) + 1) if seen[x] == 0]
    dup = sorted(x for x, c in seen.items() if c > 1)
    if missing:
        problems.append("строки исходника не попали никуда: %s" % missing[:20])
    if dup:
        problems.append("строки исходника попали дважды: %s" % dup[:20])
    # (3) с диска, независимо от сборки: голова = сгенерированная; тело = строки исходника своих диапазонов ПО ПОРЯДКУ;
    #     объединение тел = тело исходника (мультимножество строк), разница — только объявленные замены
    union = collections.Counter()
    bodies = {}
    for p, rngs in R.layout.items():
        if p not in disk:
            continue
        d = disk[p]
        if not d.startswith(R.heads[p]):
            problems.append("%s: сгенерированная голова не совпадает" % os.path.basename(p))
            continue
        body = d[len(R.heads[p]):]
        bodies[p] = body
        bl = body.splitlines(True)
        # порядок: тело — подпоследовательность исходника в возрастающем порядке строк своих диапазонов
        src_idx = [x for (a_, b_) in rngs for x in range(a_, b_ + 1)]
        if len(bl) != len(src_idx) or any(src_idx[k] >= src_idx[k + 1] for k in range(len(src_idx) - 1)):
            problems.append("%s: число строк тела %d ≠ %d строк диапазонов или порядок не возрастает" % (os.path.basename(p), len(bl), len(src_idx)))
        for ln in bl:
            union[ln] += 1
    allowed_plus, allowed_minus = collections.Counter(), collections.Counter()
    for rp in plan.get("replace", []):
        if rp["line"] <= inc_end:
            continue   # замена в прологе ядра: пролог сверяется побайтно как часть сгенерированной головы
        allowed_minus[lines[rp["line"] - 1]] += 1
        allowed_plus[replaced_line(lines, rp, eol)] += 1
    for it in (plan.get("internal") or {}).get("items", []):
        a_, b_ = it["range"]
        for o, nw in zip(lines[a_ - 1:b_], item_text(lines, it)):
            if o != nw:
                allowed_minus[o] += 1
                allowed_plus[nw] += 1
    src_body = collections.Counter(lines[inc_end:])
    extra = union - src_body
    lack = src_body - union
    if extra != allowed_plus or lack != allowed_minus:
        problems.append("мультимножество строк тела: лишних %d (допустимо %d), недостаёт %d (допустимо %d)" % (
            sum(extra.values()), sum(allowed_plus.values()), sum(lack.values()), sum(allowed_minus.values())))
        for t in list((extra - allowed_plus))[:5]:
            problems.append("  лишняя строка: %r" % t[:160])
        for t in list((lack - allowed_minus))[:5]:
            problems.append("  пропала строка: %r" % t[:160])
    # (4) вторым методом: мультимножество сущностей верхнего уровня (лексер), исходник целиком vs все файлы результата
    want, src_ents, _ = entity_counter(lines)
    got = collections.Counter()
    file_ents = {}
    for p in R.layout:
        if p not in disk:
            continue
        fl, _ = read_lines(p)
        try:
            c, ents, _L = entity_counter(fl)
        except ValueError as ex:
            problems.append("%s: лексер не разобрал файл (%s)" % (os.path.basename(p), ex))
            continue
        file_ents[p] = (fl, ents)
        if R.kind[p] == "internal":
            # голова Internal.h (шапка, #include, объявление категории) сгенерирована — в сверку идут только пункты тела
            head_n = R.heads[p][len(BOM):].count(b"\n")
            c = collections.Counter(b"".join(fl[e.first - 1:e.last]) for e in ents if e.first > head_n)
        got += c
    # допустимые замены на уровне сущностей: сущность исходника, содержащая заменённую строку, меняет текст ровно на неё
    for rp in plan.get("replace", []):
        x = rp["line"]
        for e in src_ents:
            if e.first <= x <= e.last:
                o = b"".join(lines[e.first - 1:e.last])
                nw = b"".join(replaced_line(lines, rp, eol) if y == x else lines[y - 1] for y in range(e.first, e.last + 1))
                want[o] -= 1
                want[nw] += 1
    for it in (plan.get("internal") or {}).get("items", []):
        if it.get("mode") == "inline":
            a_, b_ = it["range"]
            co, _e1, _l1 = entity_counter(lines[a_ - 1:b_])
            cn, _e2, _l2 = entity_counter(item_text(lines, it))
            want -= co
            want += cn
    want = +want
    if got != want:
        ex, ms = got - want, want - got
        problems.append("сущности: лишних %d, недостаёт %d" % (sum(ex.values()), sum(ms.values())))
        for t in list(ms)[:5]:
            problems.append("  недостаёт: %r" % t[:200])
        for t in list(ex)[:5]:
            problems.append("  лишняя: %r" % t[:200])
    # (5) ловушки и видимость файло-локальных символов
    tr = trap_check(plan, R, src_ents, file_ents)
    problems += tr["errors"]
    for s in tr["notes"]:
        print("ЗАМЕТКА " + s)
    print("исходник %s: строк %d (пролог 1..%d), сущностей %d" % (plan["original"], len(lines), inc_end, len(src_ents)))
    for p, rngs in R.layout.items():
        n = disk[p].count(b"\n") if p in disk else -1
        print("  %-12s %-80s строк %6d, кусков %d" % (R.kind[p], os.path.basename(p), n, len(rngs)))
    for s in problems:
        print("РАСХОЖДЕНИЕ " + s)
    print("ИТОГ verify: расхождений %d" % len(problems))
    return 0 if not problems else 1


def trap_check(plan, R, src_ents, file_ents):
    """Видимость файло-локальных символов и ловушки по итоговым файлам."""
    errors, notes = [], []
    internal_file = (plan.get("internal") or {}).get("file")
    # файло-локальные имена исходника (определения), из лексера исходника
    src_local = set()
    for e in src_ents:
        if e.kind != "static_func_decl":
            src_local |= e.local_names
    # где определено каждое такое имя в результате (объявление static_func_decl — не определение)
    defs = collections.defaultdict(set)
    decls = collections.defaultdict(set)
    extern_decl = collections.defaultdict(set)
    for p, (fl, ents) in file_ents.items():
        for e in ents:
            hv0 = [t.val for t in head_tokens(e.toks)]
            if p == internal_file and hv0 and hv0[0] == "extern":
                extern_decl[data_name(head_tokens(e.toks))].add(p)
            for nm in e.local_names:
                if nm not in src_local:
                    continue
                if e.name.startswith("DECLARE_LOG_CATEGORY_EXTERN"):
                    extern_decl[nm].add(p)      # extern-объявление: определение обязано быть ровно одно, в любом файле
                    continue
                (decls if e.kind == "static_func_decl" else defs)[nm].add(p)
    # макросы #define после пролога: каждое упоминание — в файле, где макрос определён (или в Internal.h)
    src_L = lex(text_of(R.lines))
    src_macros = set(m.group(1) for (ln, d, dep) in src_L.pp if ln > plan["include_end"]
                     for m in [re.match(r"#\s*define\s+(\w+)", d)] if m)
    mdefs = collections.defaultdict(set)
    lexed = {}
    for p, (fl, ents) in file_ents.items():
        Lp = lexed[p] = lex(text_of(fl))
        for (ln, d, dep) in Lp.pp:
            m = re.match(r"#\s*(define|undef)\s+(\w+)", d)
            if m:
                notes.append("%-28s стр.%-5d #%s %s" % (os.path.basename(p), ln, m.group(1), m.group(2)))
                if m.group(1) == "define":
                    mdefs[m.group(2)].add(p)
    for p, (fl, ents) in file_ents.items():
        Lp = lexed[p]
        used = set(t.val for t in Lp.toks if t.kind == "id") | set(
            w for (ln, d, dep) in Lp.pp if not d.startswith(("#define", "#undef")) for w in re.findall(r"\w+", d))
        for mname in src_macros & used:
            if p not in mdefs[mname] and internal_file not in mdefs[mname]:
                errors.append("%s: макрос %s используется, но определён только в %s" % (
                    os.path.basename(p), mname, ", ".join(sorted(os.path.basename(f) for f in mdefs[mname])) or "—"))
    internal_names = set()
    if internal_file in file_ents:
        for e in file_ents[internal_file][1]:
            internal_names |= e.local_names
            if e.kind == "macro_stmt" and e.name.startswith("DECLARE_LOG_CATEGORY_EXTERN"):
                internal_names.add(e.name.split(":")[-1])
            hv = [t.val for t in head_tokens(e.toks)]
            if hv and hv[0] == "extern":
                internal_names.add(data_name(head_tokens(e.toks)))
    for nm in sorted(src_local):
        fs = defs.get(nm, set())
        # одно и то же имя у перегрузок (NetNeedLen ×2) — допустимо, если все определения в ОДНОМ файле
        if len(fs) > 1:
            errors.append("файло-локальное %s определено в нескольких файлах: %s" % (nm, ", ".join(sorted(os.path.basename(f) for f in fs))))
        if not fs:
            errors.append("файло-локальное %s исчезло (нет определения ни в одном файле)" % nm)
        if nm in extern_decl and len(fs) != 1:
            errors.append("%s: extern-объявление в %s, определений %d (нужно ровно одно)" % (nm, ", ".join(sorted(os.path.basename(f) for f in extern_decl[nm])), len(fs)))
        for f in decls.get(nm, set()):
            if f not in fs and internal_file not in fs:
                errors.append("%s: объявление static %s без определения в этом файле или в Internal.h" % (os.path.basename(f), nm))
    # ссылки: каждое упоминание файло-локального имени — в файле, где оно определено (или через Internal.h)
    for p, (fl, ents) in file_ents.items():
        own = set()
        for e in ents:
            own |= (e.local_names & src_local)
        for e in ents:
            for nm in e.refs & src_local:
                if nm in e.local_names:
                    continue
                if nm in own or nm in internal_names:
                    continue
                if nm == "LogAsdaNet":
                    errors.append("%s: стр.%d %s использует LogAsdaNet без объявления" % (os.path.basename(p), e.first, e.name))
                    continue
                errors.append("%s: стр.%d (%s %s) ссылается на файло-локальное %s из %s" % (
                    os.path.basename(p), e.first, e.kind, e.name, nm, ", ".join(sorted(os.path.basename(f) for f in defs.get(nm, ())))))
        if R.kind.get(p) != "internal" and p != plan["source"] and internal_file and not any(
                os.path.basename(internal_file).encode("utf-8") in ln for ln in fl[:200]):
            errors.append("%s: нет #include Internal.h" % os.path.basename(p))
    # то, что обязано остаться в ядре, и глобальные объекты с конструкторами (порядок инициализации между TU)
    for p, (fl, ents) in file_ents.items():
        for e in ents:
            txt = " ".join(t.val for t in head_tokens(e.toks))
            if re.search(r"\b(DEFINE_LOG_CATEGORY\w*|IMPLEMENT_\w+)\s*\(", txt) and p != plan["source"]:
                errors.append("%s: стр.%d %s — должно остаться в ядре" % (os.path.basename(p), e.first, txt[:80]))
            if e.kind in ("global_data", "static_data") and GLOBAL_CTOR_TYPES.search(txt):
                notes.append("%-28s стр.%-5d объект с конструктором (статическая инициализация): %s" % (os.path.basename(p), e.first, e.name))
            if e.kind in ("using_namespace", "template", "anon_namespace"):
                notes.append("%-28s стр.%-5d %s %s" % (os.path.basename(p), e.first, e.kind, e.name or ",".join(sorted(e.local_names))))
    # РАННЯЯ ВИДИМОСТЬ: Internal.h подключается в начале каждой части, поэтому его имена видны и тем сущностям, что в
    #   исходнике стояли ВЫШЕ определения/объявления. Если такая сущность упоминает это имя, до разноса оно разрешалось
    #   во что-то другое (или не разрешалось) — после разноса разрешение могло тихо смениться. Это запрещено.
    first_seen = {}
    for e in src_ents:
        for nm in e.local_names:
            first_seen[nm] = min(first_seen.get(nm, e.first), e.first)
    exported = set()
    for it in (plan.get("internal") or {}).get("items", []):
        a_, b_ = it["range"]
        for e in src_ents:
            if a_ <= e.first <= b_:
                exported |= e.local_names
    exported |= set(n for n in internal_names if n in first_seen)
    for e in src_ents:
        for nm in (e.refs & exported):
            if nm in e.local_names:
                continue
            if e.first < first_seen[nm]:
                errors.append("ранняя видимость: стр.%d (%s %s) упоминает %s, а в исходнике оно объявлено ниже (стр.%d)" % (
                    e.first, e.kind, e.name, nm, first_seen[nm]))
    return {"errors": errors, "notes": notes}




def main(argv=None):
    """Точка входа."""
    ap = argparse.ArgumentParser(description="Разнос .cpp по плану с доказательством неизменности")
    sub = ap.add_subparsers(dest="cmd")
    s = sub.add_parser("scan")
    s.add_argument("src")
    s.add_argument("--json")
    s.add_argument("--include-end", type=int, default=0)
    s = sub.add_parser("split")
    s.add_argument("--plan", required=True)
    s.add_argument("--write", action="store_true")
    s.add_argument("--force", action="store_true")
    s = sub.add_parser("verify")
    s.add_argument("--plan", required=True)
    a = ap.parse_args(argv)
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if a.cmd == "scan":
        return cmd_scan(a)
    if a.cmd == "split":
        return cmd_split(a)
    if a.cmd == "verify":
        return cmd_verify(a)
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
