# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
langdet.py — надёжное определение языка строки из таблиц клиента.

Почему нужен отдельный модуль: наивная проверка «декодируется как cp949 и содержит хангыль →
корейский» ЛОЖНО срабатывает на русском тексте. Русские байты cp1251 часто образуют валидные
двухбайтовые пары cp949 («Золото» → 헨依桎), а корейские байты cp949 при чтении как cp1251 дают
кириллицу («아이템» → «ѕЖАМЕЫ»). Ни один из двух признаков сам по себе не различает языки.

Критерий: осмысленность текста в кодировке.
  * русский  — почти все символы попадают в набор «кириллица + пробел + цифры + пунктуация»,
               и среди букв есть строчные (либо набор соблюдён практически идеально);
  * корейский — строка целиком декодируется cp949 и не прошла проверку на русский;
  * латиница — латинские буквы;
  * neutral  — только цифры/знаки (имена ресурсов, формат-шаблоны);
  * empty    — пусто.
"""
import re

_CYR_L = set("абвгдеёжзийклмнопрстуфхцчшщъыьэюя")
_CYR_U = set("АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ")
_PUNCT = set(" 0123456789.,!?()-:;'\"+%/[]{}<>*=&#@_|\\~^№…—–\t\r\n")
_LAT = re.compile("[A-Za-z]")

def _russian_score(s):
    """(доля символов в русском наборе, доля строчных среди кириллических букв)"""
    if not s:
        return 0.0, 0.0
    ok = sum(1 for ch in s if ch in _CYR_L or ch in _CYR_U or ch in _PUNCT)
    letters = [ch for ch in s if ch in _CYR_L or ch in _CYR_U]
    low = sum(1 for ch in letters if ch in _CYR_L)
    return ok / len(s), (low / len(letters) if letters else 0.0)

# Служебная разметка, которая не является переводимым текстом и не должна влиять на вердикт:
# цветовые/жирные теги шрифта (<FC=..>, <FB>, </F>, <FD>), плейсхолдеры подстановки
# ((name), (number), [ss1], [sd1], [sf1], [/n]) и printf-спецификаторы (%s, %d, %2lf).
_MARKUP = re.compile(r"<[^>]*>|\[[^\]]*\]|\((?:name|number|num|time|count|item)\)|%[0-9]*l?[sdfux]",
                     re.IGNORECASE)

def strip_markup(s):
    t = _MARKUP.sub(" ", s)
    return t if t.strip() else s

def lang(b):
    """'rus' | 'kor' | 'lat' | 'neutral' | 'empty'"""
    if not b or not b.strip():
        return "empty"
    s = strip_markup(b.decode("cp1251", "replace"))
    frac, lowfrac = _russian_score(s)
    has_cyr = any(ch in _CYR_L or ch in _CYR_U for ch in s)
    if has_cyr and (frac >= 0.95 or (frac >= 0.90 and lowfrac >= 0.4)):
        return "rus"
    # ПОРЯДОК ВАЖЕН: проверку cp949 делаем ДО правила о гибридах. Корейский текст, прочитанный
    # как cp1251, состоит в основном из «кириллицы» и почти не содержит латиницы, поэтому
    # гибридное правило принимало его за русский (реплики NPC уезжали в неверную кодировку).
    # Строгая проверка русскости выше уже отсекла настоящий русский текст.
    try:
        s949 = b.decode("cp949")
        if any("가" <= ch <= "힣" or "㄰" <= ch <= "㆏" for ch in s949):
            return "kor"
    except UnicodeDecodeError:
        pass
    # ГИБРИДНЫЕ строки: переведённый текст с латинскими вставками — названия продуктов
    # (GameGuard, Internet Explorer), адреса почты, чат-команды. Считаем по буквам: если
    # кириллических букв не меньше, чем латинских, строка переведена.
    if has_cyr:
        cyr = sum(1 for ch in s if ch in _CYR_L or ch in _CYR_U)
        lat = sum(1 for ch in s if ("a" <= ch <= "z") or ("A" <= ch <= "Z"))
        if cyr >= lat and cyr >= 3:
            return "rus"
    if has_cyr and frac >= 0.80 and lowfrac >= 0.3:
        return "rus"
    if _LAT.search(s):
        return "lat"
    if has_cyr:
        return "rus"
    return "neutral"

_RANK = {"rus": 3, "lat": 2, "neutral": 2, "kor": 1, "empty": 0}

def may_replace(orig_b, fanta_b):
    return _RANK[lang(fanta_b)] >= _RANK[lang(orig_b)]
