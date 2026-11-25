# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_subjugate_widen.py — ★ РАСШИРЕНИЕ ОКНА ДОСКИ ОБЪЯВЛЕНИЙ `Subjugate_WIN` 75000 ЦЕЛИКОМ (28.10.2026, запрос владельца:
#   «названия заданий наезжают на колонку уровня»; правило widen-windows-whole-keep-symmetry). [ОТСТУПЛЕНИЕ ПОРТА ПОД RU] —
#   правка АССЕТА, кода не касается (закон no-runtime-widget-fixes). Отчёт — `Reports/board_quest/CLIENT.md` §8.
#
#   ЗАМЕР (самотест `asda.SelfTest.BoardQuest` L5…L9, шрифт самого поля, 1024×768): RU — «Задание» 264 px при поле 180/200,
#   «Вид» «<Ограничение>» 79 при 64, «Состояние» «Заканчивается сегодня» 120 при 80, строка подробностей 75242 — 355 при 322;
#   EN — «Задание» 180 (впритык), «<Restriction>» 64 (впритык). Колонки строк стоят вплотную, текст по центру — лишнее
#   наезжает на соседей.
#   РЕШЕНИЕ: колонки «Вид» 64→82, «Задание» 180/200→268, «Состояние» 80→124 (левая панель +150), правая панель 322→358 (+36);
#   окно 810 → 996 (Δ = 186, левая часть −93, правая +93 — корень окна привязан к центру). Растягиваются только СРЕДНИЕ куски
#   девятисрезов и фоны во всю ширину; кнопки, ячейки наград, иконки, вкладки НЕ тянутся — смещаются. Кегль не меняется.
#
#   ДАННЫЕ: закрытая таблица `Tools/data/subjugate_widen.json` «узел → слот было/стало» — строит режим `--plan` по живому дампу
#   окна (`Tools/asda_ui_dump_window.py`, текст `Reports/board_quest/subjugate_tree2.txt`) и правилам ниже.
#   ПРИМЕНЕНИЕ (UE): узел трогается, только если его слот РОВНО «было»; уже «стало» — готово; иное — «осмотреть», окно НЕ
#   пишется (закон owner-asset-edits-never-lose). Снимок дерева до/после: потеряно 0, у неправленых изменений 0. Бэкап до записи.
#
# Запуск: python Tools/asda_ui_subjugate_widen.py --plan <дамп.txt>          (без UE — пишет таблицу)
#         python Tools/run_headless_safe.py Tools/asda_ui_subjugate_widen.py --marker "ЗАВЕРШЕНО: расширение доски"
#           --expect-asset /Game/AsdaAssets/UI/Player_UI/Quest/WBP_Subjugate_WIN        (ASDA_SUBJUGATE_WIDEN_DRYRUN=1 — сухой)
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data", "subjugate_widen.json")
ASSET = "/Game/AsdaAssets/UI/Player_UI/Quest/WBP_Subjugate_WIN"
CONTENT_DIR = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"
MARKER = "ЗАВЕРШЕНО: расширение доски"
EPS = 0.05

DL = 150                 # прирост левой панели (колонки строк)
DR = 36                  # прирост правой панели (подробности)
D = DL + DR              # прирост окна
H = D // 2               # сдвиг половин окна
TABW = 18                # прирост вкладки-зоны 108 → 126: «Страна Вечных Войн» 107 px упиралась в скруглённые углы плашки
                         #   (кадр shot_ru.png 28.10); зазоры между вкладками — как в разметке оригинала
# Новые колонки строк (x от позиции main строки): Вид [0,82), Кол-во [82,137), Ур. [135,175), Задание [175,443), Состояние [443,567).
#   Шаг «Кол-во»→«Ур.» с наложением 2 px — как в разметке оригинала (64+55 = 119 > 117).
COLS = {"TXT00": (0.0, 82.0), "TXT04": (82.0, 55.0), "TXT01": (135.0, 40.0), "TXT02": (175.0, 268.0), "TXT03": (443.0, 124.0)}
# Старые x колонок (по ним зоны мыши `*_HIT` строки сопоставляются с колонкой): строка 1 и шапка 0/64/117/157/357,
#   строки 2…20 — 0/64/117/157/337.
OLD_X = {0.0: "TXT00", 64.0: "TXT04", 117.0: "TXT01", 157.0: "TXT02", 357.0: "TXT03", 337.0: "TXT03"}


def log(m):
    """Строка журнала."""
    try:
        import unreal
        unreal.log_warning("[расширение доски] %s" % m)
    except Exception:
        print("[расширение доски] %s" % m)


def parse_dump(path):
    """Текстовый дамп `asda_ui_dump_window` → {имя: (класс, родитель, [L,T,W,H])} (узлы без слота — пропуск)."""
    out = {}
    rx = re.compile(r"off\(L(-?[\d.]+) T(-?[\d.]+) R(-?[\d.]+) B(-?[\d.]+)\)")
    for line in open(path, encoding="utf-8"):
        p = line.split()
        if len(p) < 4 or not p[2].startswith("род="):
            continue
        m = rx.search(line)
        if not m:
            continue
        out[p[0]] = (p[1], p[2][4:], [float(m.group(i)) for i in range(1, 5)])
    return out


def plan(dump):
    """Таблица {имя: {"parent", "before", "after"}} по правилам окна."""
    t = {}

    def put(name, after):
        cls, par, before = dump[name]
        if any(abs(a - b) > EPS for a, b in zip(before, after)):
            t[name] = {"parent": par, "before": before, "after": [float(x) for x in after]}

    def mv(name, dx, dw=0.0):
        L, T, W, Hh = dump[name][2]
        put(name, [L + dx, T, W + dw, Hh])

    for name, (cls, par, s) in dump.items():
        # ── корень окна: рамка-девятисрез, шапка, фон, вкладки, ручка ──
        if par == "RootCanvas":
            m = re.fullmatch(r"Subjugate_WIN_0_(\d)", name)
            if m:
                k = int(m.group(1))
                if k in (0, 3, 6): mv(name, -H)
                elif k in (1, 4, 7): mv(name, -H, D)
                else: mv(name, H)
            elif name in ("WinMd_CBar_grp", "Box_Brown_8_0", "Win_Tab_grp", "Subjugate_textmove", "Subjugate_textmove_201_HIT"):
                mv(name, -H, D)
            elif name.startswith("Subjugate_RADIO_"):
                # вкладки у левого края окна: сдвиг −H и +TABW на каждую предыдущую вкладку, ширина +TABW
                m = re.fullmatch(r"Subjugate_RADIO_(?:212_(\d)|OPT(\d)|Text(\d?))", name)
                k = int(m.group(1) or m.group(2) or 0) if m.group(3) is None else (int(m.group(3)) - 1 if m.group(3) else 0)
                mv(name, -H + TABW * k, TABW)
            # Btn_CBase_grp / _1 — кнопки по центру окна: остаются
        elif par == "WinMd_CBar_grp":
            if name == "WinMd_CBar_1_0": mv(name, 0, D)
            elif name == "WinMd_RBar_grp": mv(name, D)    # правый край шапки с «×»
        elif par == "Win_Tab_grp":
            m = re.fullmatch(r"Win_Tab_9_(\d)", name)
            if m:
                k = int(m.group(1))
                if k in (1, 4, 7): mv(name, 0, D)
                elif k in (2, 5, 8): mv(name, D)
            elif name == "Subjugate_LBOX_grp": mv(name, 0, DL)
            elif name == "Subjugate_RBOX_grp": mv(name, DL, DR)
        elif par == "Subjugate_LBOX_grp":
            if name in ("Subjugate_LBOX_10_0", "Black_11_0", "Box_WhiteOt20_12_0"): mv(name, 0, DL)
            elif re.fullmatch(r"Subjugate_BB1_\d+_grp", name): mv(name, 0, DL)
            else:
                m = re.fullmatch(r"Subjugate_BB1_CLICK_\d+_(\d)", name)
                if m:
                    k = int(m.group(1))
                    if k in (1, 6): mv(name, 0, DL)       # верх/низ рамки выбора
                    elif k in (2, 4, 7): mv(name, DL)     # правый край
        elif re.fullmatch(r"Subjugate_BB1_(\d+)_grp", par):
            row = int(re.fullmatch(r"Subjugate_BB1_(\d+)_grp", par).group(1))
            base = 0.0 if row == 0 else 2.0               # x main строки внутри группы (шапка — 0, строки — 2)
            m = re.fullmatch(r"Subjugate_BB1_\d+_(TXT0\d)", name)
            if m:
                x, w = COLS[m.group(1)]
                L, T, W, Hh = s
                put(name, [base + x, T, w, Hh])
            elif name.startswith("Box_BlueOt20_"):
                mv(name, 0, DL)                           # подсветка строки во всю ширину
            elif name.endswith("_HIT") and not name.startswith("Subjugate_BB1_"):
                key = OLD_X.get(round(s[0] - base, 1))
                if key is None:
                    raise RuntimeError("зона мыши %s строки %d: x %.1f не узнан" % (name, row, s[0]))
                x, w = COLS[key]
                put(name, [base + x, s[1], w, s[3]])
        elif par == "Subjugate_RBOX_grp":
            if name in ("Subjugate_RBOX_160_0", "Black_grp", "Box_WhiteOt20_164_0", "Subjugate_R_T5_grp", "Subjugate_R_T8_grp"):
                mv(name, 0, DR)
            elif name in ("Subjugate_R_T1", "Subjugate_R_T1_165_HIT"):
                mv(name, DR // 2)                         # заголовок панели — по центру
            elif re.fullmatch(r"Subjugate_R_T(2|3|4|9_1|10_1)|Subjugate_R_T\d+_\d+_HIT", name):
                mv(name, 0, DR)
        elif par == "Black_grp":
            mv(name, 0, DR)
        elif par in ("Subjugate_R_T5_grp", "Subjugate_R_T8_grp"):
            if name in ("Subjugate_R_T5", "Subjugate_R_T8", "Subjugate_R_T8_182_HIT"): mv(name, 0, DR)
            elif name.startswith("Box_White1Stroke_grp") or name.startswith("Box_WhiteOt20_grp"):
                mv(name, DR // 2)                         # ячейки наград — смещаются, не тянутся
    return t


def apply_ue(dry):
    """Применение таблицы к ассету (UE)."""
    import unreal
    import asda_ui_step_dryrun as DRY
    import asda_headless_tool as HT
    import asda_ui_text_block_align as T
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    table = json.load(open(DATA, encoding="utf-8"))["nodes"]
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    nodes, st = {}, [root]
    while st:
        w = st.pop()
        if w is None:
            continue
        nodes.setdefault(w.get_name(), w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))

    def offs(w):
        s = w.get_editor_property("slot")
        m = s.get_editor_property("layout_data").get_editor_property("offsets")
        return [float(m.left), float(m.top), float(m.right), float(m.bottom)]

    def near(a, b):
        return all(abs(x - y) <= EPS for x, y in zip(a, b))

    todo, ok, bad = [], 0, []
    for name, e in sorted(table.items()):
        w = nodes.get(name)
        if w is None or w.get_parent() is None or w.get_parent().get_name() != e["parent"]:
            bad.append("%s: нет узла или чужой родитель" % name); continue
        cur = offs(w)
        if near(cur, e["after"]): ok += 1
        elif near(cur, e["before"]) or any(near(cur, p) for p in e.get("prev", [])): todo.append((name, w, e["after"]))
        else: bad.append("%s: слот %s ≠ было %s / стало %s — возможна ручная правка, НЕ трогаю" % (name, cur, e["before"], e["after"]))
    for m in bad:
        log("осмотреть: %s" % m)
    log("%sготово %d, править %d, осмотреть %d" % ("[сухой] " if dry else "", ok, len(todo), len(bad)))
    if bad:
        raise RuntimeError("есть узлы «осмотреть» — окно НЕ пишется")
    if dry or not todo:
        log("%s (правок нет%s)" % (MARKER, " — сухой прогон" if dry else ""))
        return
    f = os.path.join(CONTENT_DIR, ASSET[len("/Game/"):].replace("/", os.sep) + ".uasset")
    if HT.backup_file(f, "board_client", reuse=False, log=log) is None:
        raise RuntimeError("бэкап невозможен — запись запрещена")
    before = T._snapshot(root)
    for name, w, after in todo:
        s = w.get_editor_property("slot")
        lay = s.get_editor_property("layout_data")
        lay.set_editor_property("offsets", unreal.Margin(after[0], after[1], after[2], after[3]))
        s.set_editor_property("layout_data", lay)
    after_s = T._snapshot(B.get_root_canvas(bp))
    changed = set(n for n, _, _ in todo)
    lost = [n for n in before if n not in after_s]
    foreign = [n for n in before if n in after_s and before[n] != after_s[n] and n not in changed]
    log("узлов %d → %d; правлено %d; потеряно %d; чужих изменений %d" % (len(before), len(after_s), len(todo), len(lost), len(foreign)))
    if lost or foreign:
        raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: потеряно %s, чужих %s" % (lost[:5], foreign[:5]))
    B.compile_and_save(bp)
    if not DRY.save(bp, False, "asda_ui_subjugate_widen", False):
        raise RuntimeError("не сохранён %s" % ASSET)
    unreal.log("ASDA_WRITES_ASSET: %s" % ASSET)
    log("%s (правлено %d)" % (MARKER, len(todo)))


def main():
    """`--plan <дамп>` — таблица; иначе — применение в UE."""
    if "--plan" in sys.argv:
        dump = parse_dump(sys.argv[sys.argv.index("--plan") + 1])
        t = plan(dump)
        # Прежняя редакция таблицы: её «стало» у изменённых узлов — наша же запись прошлого прогона (поле "prev"), применять
        #   поверх неё можно; любое иное состояние слота — по-прежнему «осмотреть».
        if os.path.exists(DATA):
            old = json.load(open(DATA, encoding="utf-8")).get("nodes", {})
            for n, e in t.items():
                o = old.get(n)
                prev = list(o.get("prev", [])) if o else []
                if o and o["after"] != e["after"]:
                    prev.append(o["after"])
                if prev:
                    e["prev"] = prev
        doc = {"_comment": "Расширение окна доски объявлений 75000 (Δ=%d: левая панель +%d, правая +%d). Строит Tools/asda_ui_subjugate_widen.py --plan, применяет он же в UE." % (D, DL, DR),
               "delta": D, "nodes": t}
        open(DATA, "w", encoding="utf-8", newline="\n").write(json.dumps(doc, ensure_ascii=False, indent=1, sort_keys=True) + "\n")
        log("таблица: узлов %d → %s" % (len(t), DATA))
        return
    sys.path.insert(0, HERE)
    apply_ue(bool(os.environ.get("ASDA_SUBJUGATE_WIDEN_DRYRUN")))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
