# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_caption_gametext.py — ★★★ [ПОРТ] 09.10.2026 ПОДПИСИ ОКОН ССЫЛКОЙ НА ТАБЛИЦУ GameText (мультиязычность, этап 4).
#
# ═══ ЧТО И ЗАЧЕМ ═══════════════════════════════════════════════════════════════════════════════════
#   Решение владельца 09.10.2026: «вписанный текст в окнах заменить ссылками на таблицу (точечно, через шаги
#   пост-импорта)». Оригинал ставит подпись контролу КОДОМ по IDNum из GameText (`error list.bin`), корейский текст
#   в .JUI — заглушка дизайнера (исследование `Reports/owner_1009/multilang/06_ui_captions_en.md`).
#   Шаг делает две вещи:
#     1) `ensure_string_table` — ассет StringTable `/Game/AsdaAssets/Text/ST_GameText` (ключ = id GameText,
#        исходная строка = RU-строка `RawAssets/Text/GameText.csv` без разметки <FC=..>/<FD>/<FB>/</F>);
#        добавляет недостающие ключи плана и обновляет изменившиеся, прочие записи таблицы сохраняет как есть;
#     2) `link_captions` — свойству Text узла-подписи (TextBlock) ставит ССЫЛКУ на запись таблицы
#        (`TextLibrary.text_from_string_table`) вместо вбитой строки. В редакторе подпись показывает RU-строку
#        таблицы; в игре ведущий подменяет строки таблицы текстом языка сессии (`FAsdaLoginData::Loc`).
#   Список правок — план этапа 4 (`plan.tsv`, строки «применять = да»): окно, узел, id GameText, ожидаемый текст
#   импортёра и RU-строка GameText. Каждая строка плана проверена: контрол «точно» ставит этот id в оригинале,
#   код порта эту подпись не ставит, окно не из сцены логина и не из Unused/Temp.
#
# ═══ СТОРОЖА ════════════════════════════════════════════════════════════════════════════════════════
#   • узел обязан быть TextBlock, а его текущий текст — равен ожидаемому импортёру ИЛИ RU-строке GameText (тогда
#     ссылка не меняет вид в RU), ИЛИ уже быть ссылкой на тот же ключ (идемпотентно, 0 правок);
#     иначе — пропуск с пометкой «РУЧНАЯ ПРАВКА?» (правки владельца не трогаем);
#   • ссылка на ДРУГОЙ ключ или другую таблицу — пропуск с пометкой;
#   • LOGIN-GUARD: пакеты сцены логина (`asda_login_guard.login_package_blocked`) пропускаются всегда;
#   • сверка снимков окна до/после (`snapshot`: класс, родитель, порядок, видимость и все свойства
#     `asda_ui_step_dryrun._extra_sig` каждого узла): меняться может ТОЛЬКО текст узлов плана,
#     любое другое различие — окно НЕ сохраняется, шаг падает;
#   • сохраняются только окна, где были правки (`save_if_changed`), и таблица — только если её записи изменились.
#
# ═══ ЗАПУСК ═════════════════════════════════════════════════════════════════════════════════════════
#   Сухой прогон (ничего не пишет; таблица и ссылки меняются только в памяти процесса):
#     set ASDA_UICAP_DRYRUN=1
#     python Tools\run_headless_safe.py Tools\asda_ui_caption_gametext.py --marker "ПОДПИСИ GameText: ГОТОВО"
#   Запись (редактор закрыт, -game не запущен): тот же вызов без ключа. План — `ASDA_UICAP_PLAN` (по умолчанию
#   `Reports/owner_1009/multilang/stage4/plan.tsv`). Повторный прогон — 0 правок.
#   ★ В цепочку `asda_ui_post_import.STEPS` ставить ПОСЛЕДНИМ (после шагов, которые пишут текст узлов).
import csv
import os
import re
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Ключ сухого прогона шага (его включает и `asda_ui_post_import --dry-run` через таблицу DRY_ENV).
DRYRUN_ENV = "ASDA_UICAP_DRYRUN"
# Маркер итоговой строки (по нему успех ищет run_headless_safe.py).
MARK = "ПОДПИСИ GameText: ГОТОВО"
# Имя шага в строках лога и в `[ШАГ-ДЕЛЬТА]`.
TAG = "asda_ui_caption_gametext"
# Пакет таблицы строк и её TableId (= путь объекта).
ST_PKG = "/Game/AsdaAssets/Text/ST_GameText"
ST_ID = ST_PKG + ".ST_GameText"
# Источник RU-строк GameText (тот же, что у `import_gametext.py`; ведомость правок применяется к нему заранее).
GAMETEXT_CSV = r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Text\GameText.csv"
# План этапа 4 по умолчанию (переопределяется переменной ASDA_UICAP_PLAN).
# ★ План лежит рядом с инструментами (Tools/data), а не в отчётах: шаг цепочки пост-импорта не должен зависеть от папки
#   отчётов. Исходник плана — `Reports/owner_1009/multilang/stage4/plan.tsv` (построитель `_work/build_plan.py`).
PLAN_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "asda_ui_caption_gametext_plan.tsv")
# Переменная окружения с путём плана.
PLAN_ENV = "ASDA_UICAP_PLAN"
# Разметка GameText, которой в подписи окна быть не должно: <FC=AARRGGBB>, <FC=..%08x>, <FD>, <FB>, </F>.
MARKUP_RX = re.compile(r"<FC=[0-9A-Fa-f]*(?:%08x)?>|<F[A-Z]?>|</F>")


def log(msg):
    """Строка журнала шага."""
    import unreal
    unreal.log("[подписи GameText] %s" % msg)


def strip_markup(s):
    """RU-строка GameText без разметки цвета/шрифта."""
    return MARKUP_RX.sub("", s or "")


def csv_unescape(s):
    """Значение GameText.csv → строка: «\\n» (два символа) в CSV означает перенос строки."""
    return (s or "").replace("\\n", "\n")


def plan_unescape(s):
    """Значение plan.tsv → строка (в плане перенос записан как «\\n»)."""
    return (s or "").replace("\\n", "\n")


def load_gametext():
    """id → RU-строка GameText (с переносами, без разметки)."""
    out = {}
    with open(GAMETEXT_CSV, encoding="utf-8-sig", newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 2 and row[0] != "Key":
                out[row[0]] = strip_markup(csv_unescape(row[1]))
    return out


def load_plan(path):
    """Строки плана «применять = да» → список dict(asset, node, key, expect, ru)."""
    rows = []
    with open(path, encoding="utf-8", newline="") as f:
        for r in csv.DictReader(f, delimiter="\t"):
            if r.get("применять") != "да":
                continue
            rows.append({"asset": r["asset"], "node": r["node"], "key": r["id GameText"].strip(),
                         "expect": plan_unescape(r["ожидаемый импортёром"]),
                         "ru": plan_unescape(r["RU GameText (без разметки)"])})
    return rows


def st_escape(s):
    """Строка → ячейка CSV для `ImportTableFromCSVString` (как FStringTable::ExportStrings:
    обратная косая черта-экранирование ReplaceCharWithEscapedChar + удвоение кавычек RFC 4180)."""
    e = (s.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
         .replace("'", "\\'").replace('"', '\\"'))
    return '"' + e.replace('"', '""') + '"'


def read_string_table():
    """Текущие записи ST_GameText: ключ → исходная строка."""
    import unreal
    L = unreal.StringTableLibrary
    return {str(k): str(L.get_table_entry_source_string(ST_ID, k)) for k in L.get_keys_from_string_table(ST_ID)}


def ensure_string_table(need, dry):
    """Добавить/обновить в ST_GameText записи `need` (ключ → строка); прочие записи сохранить.
    Возврат — (число добавленных, число обновлённых). Запись ассета — только при изменениях."""
    import unreal
    import asda_ui_step_dryrun as SD
    st = unreal.load_asset(ST_PKG)
    if not st:
        raise RuntimeError("нет таблицы %s" % ST_PKG)
    cur = read_string_table()
    add = sorted(k for k in need if k not in cur)
    upd = sorted(k for k in need if k in cur and cur[k] != need[k])
    for k in upd:
        log("таблица: ключ %s «%s» → «%s»" % (k, cur[k], need[k]))
    if not add and not upd:
        log("таблица: ключей %d, правок 0" % len(cur))
        return 0, 0
    merged = dict(cur)
    merged.update(need)
    body = "Key,SourceString\n" + "".join("%s,%s\n" % (st_escape(k), st_escape(merged[k]))
                                          for k in sorted(merged, key=lambda x: (len(x), x)))
    st.modify()
    if not unreal.StringTableLibrary.import_table_from_csv_string(ST_ID, body):
        raise RuntimeError("ImportTableFromCSVString не принял таблицу")
    after = read_string_table()
    bad = [k for k in merged if after.get(k) != merged[k]]
    if bad or len(after) != len(merged):
        raise RuntimeError("таблица после заливки не совпала: ключей %d/%d, расхождений %d (%s)"
                           % (len(after), len(merged), len(bad), ", ".join(bad[:5])))
    if not dry:
        unreal.log("ASDA_WRITES_ASSET: %s" % ST_PKG)
    if not SD.save(st, dry, TAG, False):
        raise RuntimeError("не сохранена таблица %s" % ST_PKG)
    log("таблица: добавлено %d, обновлено %d, всего ключей %d%s"
        % (len(add), len(upd), len(after), "  [СУХОЙ ПРОГОН]" if dry else ""))
    return len(add), len(upd)


def text_ref(t):
    """FText → (таблица, ключ) или («», «») для текста не из таблицы строк."""
    import unreal
    T = unreal.TextLibrary
    if not T.text_is_from_string_table(t):
        return "", ""
    r = T.string_table_id_and_key_from_text(t)
    r = [x for x in (r if isinstance(r, tuple) else (r,)) if not isinstance(x, bool)]
    return (str(r[0]), str(r[1])) if len(r) >= 2 else ("", "")


def find_text_nodes(bp):
    """Имя → TextBlock по всему дереву окна."""
    import unreal
    import asda_ui_step_dryrun as SD
    out = {}
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if root is None:
        return out

    def cb(w):
        """Запомнить текстовый узел."""
        if isinstance(w, unreal.TextBlock):
            out[w.get_name()] = w
    cb(root)
    SD._walk_widgets(root, cb)
    return out


def snapshot(bp):
    """Снимок окна: имя узла → (сигнатура БЕЗ текста, текст). Сигнатура — класс, родитель, порядок среди братьев,
    видимость и все свойства `asda_ui_step_dryrun._extra_sig` (слот, шрифт, цвет, выравнивание, стиль …).
    ★ Свой снимок, а не `_tree_rows`: строка `DumpWidgetTree` рвётся на переносе внутри текста (подпись 12224
    «Введи название
 клана…»), и сверка видела «чужие узлы» из обрывков строки."""
    import unreal
    import asda_ui_step_dryrun as SD
    out = {}
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    if root is None:
        return out

    def cb(w):
        """Сигнатура одного узла."""
        par = w.get_parent()
        idx = par.get_child_index(w) if par is not None else -1
        sig = SD._extra_sig(w)
        txt = ""
        if isinstance(w, unreal.TextBlock):
            cut = sig.find(" текст=«")
            if cut >= 0:
                sig, txt = sig[:cut], sig[cut:]
        out[w.get_name()] = ("%s|%s#%d|%s|%s" % (w.get_class().get_name(), par.get_name() if par else "-", idx,
                                                 w.get_editor_property("visibility"), sig), txt)
    cb(root)
    SD._walk_widgets(root, cb)
    return out


def link_window(asset, rows, dry, stats):
    """Поставить ссылки узлам одного окна; сверить снимки; сохранить при правках. Возврат — число правок."""
    import unreal
    import asda_ui_step_dryrun as SD
    from asda_login_guard import login_package_blocked
    if login_package_blocked(asset, TAG):
        stats["логин"] += len(rows)
        return 0
    bp = unreal.EditorAssetLibrary.load_asset(asset)
    if not bp:
        log("ОКНО НЕ ЗАГРУЖЕНО %s — пропуск %d строк" % (asset, len(rows)))
        stats["нет окна"] += len(rows)
        return 0
    before = snapshot(bp)
    nodes = find_text_nodes(bp)
    done = []
    wname = asset.rsplit("/", 1)[-1]
    for r in rows:
        w = nodes.get(r["node"])
        if w is None:
            log("%s/%s: УЗЛА НЕТ — пропуск" % (wname, r["node"]))
            stats["нет узла"] += 1
            continue
        t = w.get_editor_property("text")
        tid, key = text_ref(t)
        if tid:
            if tid == ST_ID and key == r["key"]:
                stats["уже ссылка"] += 1
            else:
                log("%s/%s: уже ссылка на ДРУГОЕ (%s:%s) — пропуск" % (wname, r["node"], tid, key))
                stats["чужая ссылка"] += 1
            continue
        cur = str(t)
        if cur != r["expect"] and cur != r["ru"]:
            log("%s/%s: РУЧНАЯ ПРАВКА? текст «%s» ≠ импортёру «%s» и ≠ GameText «%s» — пропуск"
                % (wname, r["node"], cur, r["expect"], r["ru"]))
            stats["ручная правка"] += 1
            continue
        w.set_editor_property("text", unreal.TextLibrary.text_from_string_table(ST_ID, r["key"]))
        tid2, key2 = text_ref(w.get_editor_property("text"))
        if tid2 != ST_ID or key2 != r["key"]:
            raise RuntimeError("%s/%s: ссылка не встала (%s:%s)" % (wname, r["node"], tid2, key2))
        done.append(r["node"])
        log("%s/%s: «%s» → ST_GameText:%s («%s»)" % (wname, r["node"], cur, r["key"], r["ru"]))
    if not done:
        return 0
    after = snapshot(bp)
    bad = []
    for n in sorted(set(before) | set(after)):
        a, b = before.get(n), after.get(n)
        if a == b:
            continue
        if a is None or b is None:
            bad.append("узел %s пропал/появился" % n)
        elif n not in done:
            bad.append("изменён чужой узел %s" % n)
        elif a[0] != b[0]:
            bad.append("у %s изменилось не только свойство Text" % n)
    if bad:
        raise RuntimeError("%s: СВЕРКА СНИМКОВ НЕ ПРОШЛА — окно не сохраняю: %s" % (wname, "; ".join(bad[:8])))
    if not dry:
        unreal.log("ASDA_WRITES_ASSET: %s" % asset)
    SD.save_if_changed(bp, dry, TAG, None, len(done), None, unreal.AsdaUmgBuilder.compile_and_save)
    stats["поставлено"] += len(done)
    stats["окон"] += 1
    return len(done)


def main():
    """Таблица → ссылки в окнах плана → итог с маркером."""
    import asda_ui_step_dryrun as SD
    dry = SD.is_dry(DRYRUN_ENV)
    plan_path = os.environ.get(PLAN_ENV) or PLAN_DEFAULT
    rows = load_plan(plan_path)
    gt = load_gametext()
    miss = sorted({r["key"] for r in rows if r["key"] not in gt})
    if miss:
        raise RuntimeError("в GameText.csv нет ключей плана: %s" % ", ".join(miss[:10]))
    for r in rows:   # RU-строка берётся из GameText.csv СЕЙЧАС (после ведомости правок), а не из плана
        r["ru"] = gt[r["key"]]
    need = {r["key"]: gt[r["key"]] for r in rows}
    log("план %s: строк %d, окон %d, ключей %d%s" % (plan_path, len(rows), len({r["asset"] for r in rows}),
                                                    len(need), "  [СУХОЙ ПРОГОН]" if dry else ""))
    added, updated = ensure_string_table(need, dry)
    stats = {k: 0 for k in ("поставлено", "окон", "уже ссылка", "ручная правка", "чужая ссылка", "нет узла",
                            "нет окна", "логин")}
    by_asset = {}
    for r in rows:
        by_asset.setdefault(r["asset"], []).append(r)
    for asset in sorted(by_asset):
        link_window(asset, by_asset[asset], dry, stats)
    log("%s%s — таблица +%d/~%d; ссылок %d в окнах %d; уже ссылка %d; пропуски: ручная правка %d, чужая ссылка %d, "
        "нет узла %d, нет окна %d, логин %d"
        % (MARK, "  [СУХОЙ ПРОГОН — на диск не записано ничего]" if dry else "", added, updated,
           stats["поставлено"], stats["окон"], stats["уже ссылка"], stats["ручная правка"], stats["чужая ссылка"],
           stats["нет узла"], stats["нет окна"], stats["логин"]))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    try:
        main()
    except Exception:                                            # noqa: BLE001
        import unreal
        unreal.log_error("[подписи GameText] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
