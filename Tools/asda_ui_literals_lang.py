# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
r"""
asda_ui_literals_lang.py — ОФЛАЙН-генератор оверлеев «вбитые надписи WBP» (★ [ПОРТ] 10.10.2026, полнота английской
версии, отчёт `Reports/owner_1010/en_completeness.md`). Читает в игре C++ (`FAsdaLang::ApplyUiLiterals`,
`Source/AsdaReborn/Private/Text/AsdaLangUiAssets.cpp`): полиглот-данные UE «ключ текста → перевод», сверка по
русскому исходнику (поменяли исходник в ассете — движок перевод не применит, правка владельца побеждает).

Запуск (из каталога AstoryClient):
    set PYTHONIOENCODING=utf-8 && python Tools\asda_ui_literals_lang.py --dump <ui_literals_dump.tsv> --lang en
        [--out RawAssets\Text\L10N\en] [--check]
  * `--dump` — выгрузка игры `asda.Lang.DumpUiLiterals out=<файл>` (окно, узел, свойство, вид, пространство имён,
    ключ, исходник; только чтение ассетов);
  * `--check` — только отчёт о пробелах, файлы не пишутся;
  * `--check-terms` (★ 10.10.2026, en_terms_fix) — без `--dump`: ВСЕ ручные переводы языка (эта ведомость и ведомости
    «ключ → значение» `Tools/data/loc/<lang>/*.json`) против словаря терминов английского оригинала
    `Tools/data/loc_terms_<lang>.json`; нарушения — в консоль, код возврата 1. Файлы не пишутся.

Источники перевода (по приоритету):
  1) ведомость `Tools/data/loc/<lang>/UiLiteral.json` — ручной перевод по русскому исходнику (`ru` — исходник
     в экранированном виде, как в выгрузке; необязательные `asset`/`node` сужают запись до одного узла);
  2) план подписей этапа 4 `Tools/data/asda_ui_caption_gametext_plan.tsv` — EN оригинала для ЭТОГО узла (id GameText,
     который код оригинала ставит в контрол по IDNum; столбец «EN (оригинал)»);
  3) точное совпадение исходника со строкой GameText (RU `RawAssets/Text/GameText.csv` → EN-оверлей
     `RawAssets/Text/L10N/<lang>/DT_Loc_GameText_<lang>.csv`); несколько id с разным EN — берётся самый частый,
     остальные пишутся в `_sources_UiLiteral.tsv`.
Не переводятся: окна Unused/Temp/Dev (в игре не открываются), WBP_LANG_SELECT (двуязычное по замыслу), свойства
класса (PaletteCategory — служебное), тексты без кириллицы (корейские заглушки дизайнера есть и в RU-версии),
тексты вида stringtable (их переводит таблица подписей ST_GameText) и invariant (без ключа — полиглот их не видит;
список — в `_gaps_UiLiteral.tsv` с пометкой).

Пишет ТОЛЬКО в --out: `DT_Loc_UiLiteral_<lang>.csv` (перевод), `DT_Loc_UiLiteralSrc_<lang>.csv` (русский исходник),
оба — UTF-8 BOM, CRLF, `Name,Text`, ключ строки = ключ текста (`<ns>|<key>` при непустом пространстве имён), перевод
строки в значении — `\n` двумя символами; `_sources_UiLiteral.tsv`, `_gaps_UiLiteral.tsv`. Импорт в ассеты —
штатный `Tools/import_text_lang.py` (ASDA_LANG_ONLY=UiLiteral,UiLiteralSrc).
Импорт модуля без побочных эффектов (закон tools-no-import-side-effects).
"""
import argparse
import collections
import csv
import io
import json
import os
import re
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))            # AstoryClient\Tools
PORT = os.path.dirname(TOOLS)                                  # AstoryClient
GAMETEXT_RU = os.path.join(PORT, "RawAssets", "Text", "GameText.csv")          # RU-источник DT_GameText
PLAN = os.path.join(TOOLS, "data", "asda_ui_caption_gametext_plan.tsv")       # план подписей этапа 4
SKIP_DIRS = ("/Unused/", "/Temp/", "/Dev/")                                   # окна вне игры
SKIP_ASSETS = ("/WBP_LANG_SELECT",)                                           # двуязычно по замыслу
CYR = re.compile(u"[\u0400-\u04FF]")                                          # кириллица
MARKUP = re.compile(r"<F[^>]*>|</F>")                                         # inline-теги GameText


def norm(s):
    """Ключ сравнения строк: без разметки, экранированные переводы строк → пробел, пробелы схлопнуты, края срезаны."""
    s = MARKUP.sub("", s or "")
    s = s.replace("\\n", " ").replace("\\r", " ").replace("\\t", " ")
    return re.sub(r"\s+", " ", s).strip()


def read_csv(path):
    """CSV «ключ,значение» (с заголовком) → {ключ: значение}."""
    out = {}
    with io.open(path, encoding="utf-8-sig", newline="") as fh:
        rows = list(csv.reader(fh))
    for r in rows[1:]:
        if len(r) >= 2:
            out[r[0]] = r[1]
    return out


def write_csv(path, rows):
    """UTF-8 BOM, CRLF, `Name,Text` (формат оверлеев `extract_text_lang.py`)."""
    with io.open(path, "w", encoding="utf-8-sig", newline="") as fh:
        w = csv.writer(fh, lineterminator="\r\n")
        w.writerow(["Name", "Text"])
        for k, v in rows:
            w.writerow([k, v])


def load_plan():
    """План этапа 4: (asset, node) → (id, EN оригинала)."""
    out = {}
    with io.open(PLAN, encoding="utf-8-sig") as fh:
        rows = list(csv.reader(fh, delimiter="\t"))
    head = rows[0]
    ia, ino, iid, ien = head.index("asset"), head.index("node"), head.index("id GameText"), head.index("EN (оригинал)")
    for r in rows[1:]:
        if len(r) > ien and r[ien].strip():
            out[(r[ia], r[ino])] = (r[iid], r[ien])
    return out


def load_ledger(lang):
    """Ведомость ручного перевода: (asset|None, node|None, ru) → (en, why, source)."""
    path = os.path.join(TOOLS, "data", "loc", lang, "UiLiteral.json")
    out = {}
    if not os.path.exists(path):
        return out, path
    if TOOLS not in sys.path:
        sys.path.insert(0, TOOLS)
    import asda_ledger as _L       # ★ дедуп №2 В2 Т8: общий читатель JSON-ведомостей (все поля, порядок строк)
    for r in _L.read_rows(path):
        out[(r.get("asset"), r.get("node"), r["ru"])] = (r["en"], r.get("why", ""), r.get("source", "manual"))
    return out, path


TERMS = os.path.join(TOOLS, "data", "loc_terms_%s.json")                     # словарь терминов оригинала (--check-terms)


def ledger_pairs(lang):
    """★ 10.10.2026 (en_terms_fix): все ручные строки языка — (где, RU-или-KO исходник, перевод).
    UiLiteral — из своей ведомости (ключ = русский исходник); ведомости «ключ → значение» (`Tools/data/loc/<lang>/<Т>.json`)
    — исходник берётся из `_sources_<Т>.tsv` генератора `extract_text_lang.py` (строки с источником `ledger`)."""
    out = []
    led_dir = os.path.join(TOOLS, "data", "loc", lang)
    l10n = os.path.join(PORT, "RawAssets", "Text", "L10N", lang)
    for fn in sorted(os.listdir(led_dir)):
        if not fn.endswith(".json"):
            continue
        tn = fn[:-5]
        with io.open(os.path.join(led_dir, fn), encoding="utf-8") as fh:
            rows = json.load(fh).get("rows", [])
        if tn == "UiLiteral":
            out.extend(("UiLiteral «%s»" % r["ru"], r["ru"], r["en"]) for r in rows)
            continue
        src = {}   # ключ → исходник (RU или корейский/японский оригинал строки)
        sp = os.path.join(l10n, "_sources_%s.tsv" % tn)
        if os.path.exists(sp):
            with io.open(sp, encoding="utf-8-sig") as fh:
                for r in csv.reader(fh, delimiter="\t", quoting=csv.QUOTE_NONE):
                    if len(r) >= 4 and r[1] == "ledger":
                        src[r[0]] = r[2]
        out.extend(("%s:%s" % (tn, r["key"]), src.get(r["key"], ""), r["value"]) for r in rows)
    return out


def check_terms(lang):
    """★ 10.10.2026 (en_terms_fix): ручные переводы против словаря терминов оригинала (`Tools/data/loc_terms_<lang>.json`).
    Печатает нарушения; возвращает их число (0 — всё по оригиналу)."""
    with io.open(TERMS % lang, encoding="utf-8") as fh:
        terms = json.load(fh)["terms"]
    bad = 0
    for where, src, en in ledger_pairs(lang):
        for t in terms:
            hit = any(t.get(f) and re.search(t[f], src, re.I) for f in ("ru", "ko"))
            if not hit or (t.get("unless") and re.search(t["unless"], src, re.I)):
                continue
            why = []
            if t.get("bad") and re.search(t["bad"], en):
                why.append("есть «%s»" % re.search(t["bad"], en).group(0))
            if t.get("need") and not re.search(t["need"], en):
                why.append("нет «%s»" % t["need"])
            if why:
                bad += 1
                print("  ТЕРМИН\t%s\t%s\t→ %s\t(%s; оригинал: %s — %s)" % (where, src, en, ", ".join(why), t["en"], t["evidence"]))
    print("проверка терминов (%s): строк %d, нарушений %d" % (lang, len(ledger_pairs(lang)), bad))
    return bad


def main():
    """Сборка оверлеев UiLiteral/UiLiteralSrc из выгрузки."""
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=False)
    ap.add_argument("--lang", default="en")
    ap.add_argument("--out", default=None)
    ap.add_argument("--check", action="store_true")
    # ★ 10.10.2026 (en_terms_fix): проверка ручных переводов по словарю терминов оригинала; файлы не пишутся.
    ap.add_argument("--check-terms", action="store_true")
    a = ap.parse_args()
    if a.check_terms:
        return 1 if check_terms(a.lang) else 0
    if not a.dump:
        ap.error("нужен --dump (или --check-terms)")
    out_dir = a.out or os.path.join(PORT, "RawAssets", "Text", "L10N", a.lang)
    en_gt = read_csv(os.path.join(PORT, "RawAssets", "Text", "L10N", a.lang, "DT_Loc_GameText_%s.csv" % a.lang))
    ru_gt = read_csv(GAMETEXT_RU)
    # RU-строка → счётчик EN по всем id с этой строкой
    by_ru = collections.defaultdict(collections.Counter)
    for k, ru in ru_gt.items():
        en = en_gt.get(k, "")
        if en and not CYR.search(en):
            by_ru[norm(ru)][en] += 1
    plan = load_plan()
    ledger, ledger_path = load_ledger(a.lang)
    used_ledger = set()

    with io.open(a.dump, encoding="utf-8") as fh:
        rows = list(csv.reader(fh, delimiter="\t", quoting=csv.QUOTE_NONE))
    head = rows[0]
    col = {n: i for i, n in enumerate(head)}
    tr, src, sources, gaps = {}, {}, [], []
    stat = collections.Counter()
    for r in rows[1:]:
        if len(r) < len(head):
            continue
        asset, node, prop, kind, ns, key, ru = (r[col[c]] for c in ("asset", "node", "prop", "kind", "ns", "key", "source"))
        if any(d in asset + "/" for d in SKIP_DIRS) or asset.endswith(SKIP_ASSETS) or node == "(класс)":
            stat["пропуск: вне игры/служебное"] += 1
            continue
        if not CYR.search(ru):
            stat["пропуск: без кириллицы"] += 1
            continue
        if kind == "stringtable":
            stat["stringtable (ST_GameText)"] += 1
            continue
        if kind != "literal":
            gaps.append((asset, node, prop, kind, ru, "", "текст без ключа (%s) — полиглот не применим" % kind))
            stat["без ключа: %s" % kind] += 1
            continue
        row_key = (ns + "|" + key) if ns else key
        en, how = None, ""
        for lk in ((asset, node, ru), (None, None, ru)):
            if lk in ledger:
                en, why, s = ledger[lk]
                how = "ведомость (%s): %s" % (s, why)
                used_ledger.add(lk)
                break
        if en is None and (asset, node) in plan:
            pid, pen = plan[(asset, node)]
            ru_of_id = norm(ru_gt.get(pid, ""))
            # EN узла по IDNum — если исходник узла и есть эта строка GameText (без разметки) или смысл тот же
            #   (план этапа 4 сопоставил узел и id по коду оригинала); иначе — только как подсказка в журнал.
            if ru_of_id == norm(ru):
                en, how = en_gt.get(pid) or pen, "план этапа 4: id %s (узел по IDNum, RU совпал)" % pid
        if en is None:
            c = by_ru.get(norm(ru))
            # Однозначно только если у всех id с этой RU-строкой EN один и тот же (с точностью до регистра/пробелов/
            #   точки в конце): иначе смысл зависит от контекста окна — такой текст уходит в ведомость (ручной выбор).
            if c and len({norm(e).rstrip(".").lower() for e in c}) == 1:
                en = c.most_common(1)[0][0]
                how = "GameText: точное совпадение RU (однозначно)"
        # План этапа 4 при ДРУГОЙ редакции RU узла (IDNum → id) не используется: сверка 10.10.2026 показала ложные пары
        #   («Атаковать» → «Sit/Stand up», «Обмен снаряжением» → «Turn Right») — такие узлы переводятся в ведомости.
        if en is None:
            gaps.append((asset, node, prop, kind, ru, row_key, "нет перевода"))
            stat["нет перевода"] += 1
            continue
        if not how.startswith("ведомость"):
            # EN из таблицы текстов: inline-теги (<FD>…</F>) во вбитой надписи показались бы как есть — снимаются;
            #   края — по образцу RU (лишние пробелы в конце EN-строк оригинала не нужны).
            en = MARKUP.sub("", en)
            if ru == ru.strip():
                en = en.strip()
        en = en.replace("\r\n", "\\n").replace("\n", "\\n")
        if row_key in tr and tr[row_key] != en:
            how += " [конфликт ключа: было «%s»]" % tr[row_key]
        tr[row_key] = en
        src[row_key] = ru
        sources.append((asset, node, prop, row_key, ru, en, how))
        stat["переведено"] += 1

    unused = [k for k in ledger if k not in used_ledger]
    print("UiLiteral (%s): %s" % (a.lang, ", ".join("%s %d" % kv for kv in sorted(stat.items()))))
    print("уникальных ключей: %d; строк ведомости: %d, не использовано: %d (%s)" % (len(tr), len(ledger), len(unused), ledger_path))
    gap_ru = collections.Counter(g[4] for g in gaps if g[6] == "нет перевода")
    print("уникальных исходников без перевода: %d" % len(gap_ru))
    if a.check:
        for ru, n in gap_ru.most_common():
            print("  ПРОБЕЛ\t%d\t%s" % (n, ru))
        return 0
    os.makedirs(out_dir, exist_ok=True)
    write_csv(os.path.join(out_dir, "DT_Loc_UiLiteral_%s.csv" % a.lang), sorted(tr.items()))
    write_csv(os.path.join(out_dir, "DT_Loc_UiLiteralSrc_%s.csv" % a.lang), sorted(src.items()))
    with io.open(os.path.join(out_dir, "_sources_UiLiteral.tsv"), "w", encoding="utf-8-sig", newline="") as fh:
        fh.write("asset\tnode\tprop\tключ\tRU\tEN\tисточник\r\n")
        for s in sources:
            fh.write("\t".join(s) + "\r\n")
    with io.open(os.path.join(out_dir, "_gaps_UiLiteral.tsv"), "w", encoding="utf-8-sig", newline="") as fh:
        fh.write("asset\tnode\tprop\tвид\tRU\tключ\tпричина\r\n")
        for g in gaps:
            fh.write("\t".join(g) + "\r\n")
    print("записано: %s" % out_dir)
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main())
