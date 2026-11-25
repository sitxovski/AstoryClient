# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_label_band.py — ★★★ НЕВИДИМАЯ ПОЛОСА ПОД ПОДПИСЬЮ ЗА РАМКОЙ ОКНА (07.10.2026, решение владельца; отчёт
#   `Reports/owner_1007/label_band_and_8500.md`, ревизия-находка — `widen_fixes3_REVIEW.md` §4 / М-1).
#   Правка АССЕТОВ (закон no-runtime-widget-fixes), перегенерации UI нет (закон no-ui-regeneration-without-permission).
#
#   ДЕФЕКТ. Импортёр ставил узел-подпись (TextBlock) с верхом строки = верх контрола + TextPos.y + vy (vy — запечённая
#   однострочная центровка по маске .JUI: (h − 14)/2 у 0x4, h − 14 у 0x8), а высоту оставлял ПОЛНОЙ max(h, 14) —
#   узел вылезал за низ контрола на TextPos.y + vy. Хит-тест окна 1:1 с оригиналом (`HitTestWidgetTree`,
#   `AsdaWindowSubsystem.cpp`; зона чата — `AsdaChatWindowSubsystem::GetWindowScreenRect`) меряет прямоугольник узла,
#   поэтому у узла, вылезшего ЗА РАМКУ ОКНА, полоса под рамкой съедала щелчок в мир (магазин NPC 9.5 px, чат 7 px).
#   В оригинале контрол — прямоугольник Origin/Size, текст внутри; полосы нет.
#
#   ПРАВКА. Общая точка `asda_jui_parse.label_slot`: верх строки прежний (пиксель в пиксель), у узла, который вылез
#   бы за низ рамки окна, — низ по низу своего контрола (не меньше строки 14). Импортёр (`asda_jui_generic_umg`) с
#   07.10 строит так сам; этот шаг приводит к тому же виду УЖЕ СОБРАННЫЕ ассеты — по ЗАКРЫТОЙ таблице
#   `Tools/data/ui_label_band.json` «узел → слот было/стало». Таблицу строит этот же файл (`--plan --write`) по
#   офлайн-прогону импортёра `asda_jui_importer_sim.py` с прежним и новым правилом: в таблице ровно те узлы, у которых
#   новое правило меняет слот (только высоту), «было»/«стало» — точный вид импортёра. Поэтому после перегенерации
#   шаг находит 0 правок («сходится к одному итогу»).
#
#   БЕЗОПАСНОСТЬ (закон owner-asset-edits-never-lose): узел правится, только если его слот (смещения, анкоры,
#   alignment, авторазмер) в ТОЧНОСТИ «было» (вид импортёра); «стало» — готово; слот, который ведёт ДРУГОЙ шаг
#   (`foreign` таблицы, с именем шага) — «чужой шаг», не трогается; любой иной вид — «осмотреть»: окно не пишется,
#   прогон падает. Снимок дерева до/после: узлов не потеряно, чужие узлы не изменились, у правленых изменилась ТОЛЬКО
#   высота слота. Ручной запуск пишет только при бэкапе того же размера (закон backup-before-edit).
#   ★ 07.10 (ревизия `label_band_and_8500_REVIEW.md`, СРЕДН-1): ВСЯ ПАПКА `/UI/Login/` — мимо шага (решение владельца:
#   окна логина не трогать). Прежний пропуск только по `login_package_blocked` пропустил `WBP_AD_Win` (его нет в
#   списке пакетов сторожа) — записан `AD_Contents` 598→306, ассет восстановлен из бэкапа. Теперь: (1) `--plan`
#   окна логина в таблицу не кладёт (только в скан, «логин — только отчёт»); (2) `main` пропускает их по пути
#   ДО загрузки, даже если строка в таблице появится; сторож логина остаётся второй проверкой.
#
# Запуск:
#   вне редактора:  python Tools\asda_ui_label_band.py --plan [--write]   — таблица по офлайн-прогону импортёра
#                   python Tools\asda_ui_label_band.py --scan             — таблица «окно / узел / выход за рамку, px»
#   коммандлет (редактор и игра ЗАКРЫТЫ):
#     python Tools\run_headless_safe.py Tools\asda_ui_label_band.py --commandlet-rhi --marker "ПОЛОСА-ПОДПИСИ: ГОТОВО"
#   ASDA_LABELBAND_DRYRUN=1 — сухой прогон; ASDA_LABELBAND_ONLY=<подстрока ассета> — только эти окна.
import json
import os
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

DATA = os.path.join(HERE, "data", "ui_label_band.json")
# Бэкап до записи (★ Т3, 11.10.2026): набор `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_labelband\<путь от @@ASTORY_DEV_ROOT@@>`
#   (`T._ensure_backup` → `asda_headless_tool.backup_file`; была прошитая папка `2026-10-07_labelband`)
BACKUP_SET = "labelband"
# ASDA_LABELBAND_BACKUP — готовая папка набора целиком (переопределение, как прежде); не задана — None
BACKUP_DIR_OVERRIDE = os.environ.get("ASDA_LABELBAND_BACKUP") or None
EPS = 0.05
# ★ 07.10 (СРЕДН-1 ревизии): папка окон сцены логина — шаг её не пишет никогда (ни таблица, ни применение).
LOGIN_DIR = "/UI/Login/"


def is_login_asset(asset):
    """True — ассет окна сцены логина (папка `/UI/Login/`): только отчёт, без записи."""
    return LOGIN_DIR in asset

# ★ УЗЛЫ, ЧЕЙ СЛОТ ВЕДЁТ ДРУГОЙ ШАГ ПОСТ-ИМПОРТА (снято полным структурным дампом 07.10, `labelband_logs`).
#   Узел в таком виде шаг не трогает и «осмотреть» не пишет: его итог одинаков при любом входе (тот шаг пишет слот
#   целиком), а свою сверку «было» тот шаг принимает в обоих видах импортёра.
#   • 77000 `ITEM_making_B_tooltip_2_okbtn_Text` — подпись «Создание предмета» правее иконки, кегль 7:
#     `asda_ui_text_block_align` (а) CAPTION_FIX (26.09). Её кнопка лежит в под-окне 77200 ниже рамки 77000.
#     ★ 07.10 (ревизия МЕЛК-3): CAPTION_FIX считает высоту через `J.label_slot` в виде входа — у собранного ассета
#     итог прежний (высота 20), после перегенерации (вход «по низу кнопки») — 16.5; принимаются оба.
#   Значение: (шаг, [допустимые слоты]).
FOREIGN = {
    ("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_ITEM_making_R_BOX", "ITEM_making_B_tooltip_2_okbtn_Text"):
        ("asda_ui_text_block_align (а) CAPTION_FIX",
         [[[21.0, 3.5, 96.0, 20.0], [0.0, 0.0, 0.0, 0.0], [0.0, 0.0], False],
          [[21.0, 3.5, 96.0, 16.5], [0.0, 0.0, 0.0, 0.0], [0.0, 0.0], False]]),
}


def log(m):
    """Строка журнала шага."""
    if unreal:
        unreal.log("[полоса-подписи] %s" % m)
    else:
        print("[полоса-подписи] %s" % m)


# ══════════════ ТАБЛИЦА (вне редактора) ══════════════
def _changed(legacy, new):
    """Узлы, у которых новое правило меняет слот: {ассет: [(строка legacy, строка new)]} (сверка по порядку обхода)."""
    out = {}
    for p, L in legacy.items():
        rows_n = new[p]["rows"]
        if len(rows_n) != len(L["rows"]):
            raise RuntimeError("%s: число узлов разошлось (%d / %d)" % (p, len(L["rows"]), len(rows_n)))
        for a, b in zip(L["rows"], rows_n):
            if a["n"] != b["n"]:
                raise RuntimeError("%s: порядок узлов разошёлся (%s / %s)" % (p, a["n"], b["n"]))
            if a["slot"] != b["slot"]:
                out.setdefault(p, []).append((a, b))
    return out


def _abs_rects(rows):
    """Прямоугольники узлов в координатах базы 1024×768 (анкоры корня и группы учитываются)."""
    by = {r["n"]: r for r in rows}
    cache = {}

    def ab(n):
        if n in cache:
            return cache[n]
        r = by[n]
        s = r["slot"]
        if s is None:
            cache[n] = (0.0, 0.0, 1024.0, 768.0)
            return cache[n]
        off, anch, al = s[0], s[1], s[2]
        px, py, pw, ph = (0.0, 0.0, 1024.0, 768.0) if r["p"] in ("RootCanvas", "-") else ab(r["p"])
        if abs(anch[0] - anch[2]) < 1e-6:
            w = off[2]
            x = px + anch[0] * pw + off[0] - al[0] * w
        else:
            x = px + anch[0] * pw + off[0]
            w = (anch[2] - anch[0]) * pw - off[0] - off[2]
        if abs(anch[1] - anch[3]) < 1e-6:
            h = off[3]
            y = py + anch[1] * ph + off[1] - al[1] * h
        else:
            y = py + anch[1] * ph + off[1]
            h = (anch[3] - anch[1]) * ph - off[1] - off[3]
        cache[n] = (x, y, w, h)
        return cache[n]
    return {n: ab(n) for n in by if n != "RootCanvas"}


def build_plan():
    """Два офлайн-прогона импортёра → (таблица, строки скана). Строка скана: (ассет, окно, узел, выход вниз было/стало)."""
    import asda_jui_importer_sim as SIM
    legacy = SIM.simulate(legacy=True)
    new = SIM.simulate(legacy=False)
    ch = _changed(legacy, new)
    table, scan = {}, []
    for p, pairs in sorted(ch.items()):
        L = legacy[p]
        rl = _abs_rects(L["rows"])
        rn = _abs_rects(new[p]["rows"])
        fl, ft, fw, fh = L["root_abs"]
        nodes = {}
        for a, b in pairs:
            # правило обязано менять у подписи только высоту (у верхнего уровня с тянущимся Y — низ анкора)
            if a["c"] != "TextBlock" or a["slot"][0][:3] != b["slot"][0][:3] or a["slot"][2:] != b["slot"][2:]                     or a["slot"][1][:3] != b["slot"][1][:3]:
                raise RuntimeError("%s.%s: правило изменило не только высоту подписи" % (p, a["n"]))
            ent = {"parent": a["p"], "before": a["slot"], "after": b["slot"], "class": a["c"]}
            fo = FOREIGN.get((p, a["n"]))
            if fo:
                ent["foreign"] = {"step": fo[0], "slots": fo[1]}
            nodes[a["n"]] = ent
            x, y, w, h = rl[a["n"]]
            _x2, y2, _w2, h2 = rn[a["n"]]
            scan.append((p, L["window"], a["n"], max(0.0, y + h - (ft + fh)), max(0.0, y2 + h2 - (ft + fh))))
        if is_login_asset(p):
            continue                     # ★ 07.10: окна логина — только в скан (отчёт), в таблицу шага не попадают
        table[p] = {"window": L["window"], "class": L["class"], "root_abs": L["root_abs"], "nodes": nodes}
    return table, scan


# ══════════════ ПРИМЕНЕНИЕ (редактор) ══════════════
def _tb():
    """Общие помощники `asda_ui_text_block_align` (обход, снимок, слоты, бэкап)."""
    import asda_ui_text_block_align as T
    return T


def _near(a, b):
    """Покомпонентное равенство с допуском EPS (вложенные списки)."""
    if isinstance(a, (list, tuple)):
        return isinstance(b, (list, tuple)) and len(a) == len(b) and all(_near(x, y) for x, y in zip(a, b))
    if isinstance(a, bool) or isinstance(b, bool):
        return bool(a) == bool(b)
    return abs(float(a) - float(b)) <= EPS


def _sig(w):
    """[смещения, анкоры, alignment, авторазмер] слота канваса узла (как в таблице) или None."""
    s = _tb()._slot_sig(w)
    if s is None:
        return None
    return [list(s[0]), list(s[1]), list(s[2]), bool(s[3])]


def _verdict(name, ent, w):
    """('fixed'|'ok'|'foreign'|'inspect', текст)."""
    if w is None:
        return "inspect", "%s: узла нет" % name
    if not isinstance(w, unreal.TextBlock):
        return "inspect", "%s: не TextBlock" % name
    cur = _sig(w)
    if cur is None:
        return "inspect", "%s: слот не канвасный" % name
    par = w.get_parent().get_name() if w.get_parent() else "-"
    if par != ent["parent"]:
        return "inspect", "%s: родитель %s, в таблице %s" % (name, par, ent["parent"])
    if _near(cur, ent["after"]):
        return "ok", "%s: уже по низу контрола %s" % (name, ent["after"][0])
    if _near(cur, ent["before"]):
        return "fixed", "%s: %s → %s" % (name, cur[0], ent["after"][0])
    fo = ent.get("foreign")
    if fo and any(_near(cur, sl) for sl in (fo.get("slots") or [fo["slot"]])):
        return "foreign", "%s: слот ведёт шаг %s — не трогаю" % (name, fo["step"])
    return "inspect", "%s: слот %s не «было» %s и не «стало» %s — НЕ трогаю (возможна ручная правка)" \
        % (name, cur, ent["before"], ent["after"])


def _apply(ent, w):
    """Записать «стало» (меняется только высота, анкоры — если «стало» их меняет: верхний уровень с тянущимся Y)."""
    T = _tb()
    s = T._slot(w)
    lay = s.get_editor_property("layout_data")
    a = ent["after"]
    lay.set_editor_property("offsets", unreal.Margin(a[0][0], a[0][1], a[0][2], a[0][3]))
    if not _near(ent["before"][1], a[1]):
        lay.set_editor_property("anchors", unreal.Anchors(unreal.Vector2D(a[1][0], a[1][1]),
                                                          unreal.Vector2D(a[1][2], a[1][3])))
    s.set_editor_property("layout_data", lay)
    got = _sig(w)
    if not _near(got, a):
        raise RuntimeError("%s: после записи слот %s ≠ %s — НЕ сохраняю" % (w.get_name(), got, a))


def main(standalone=False):
    """Применить таблицу DATA. `standalone` — ручной запуск: бэкап обязателен."""
    T = _tb()
    dry = bool(os.environ.get("ASDA_LABELBAND_DRYRUN"))
    only = os.environ.get("ASDA_LABELBAND_ONLY", "")
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    from asda_login_guard import login_package_blocked
    with open(DATA, encoding="utf-8") as f:
        table = json.load(f)
    fixed_total = ok_total = foreign_total = 0
    inspect = []
    for asset in sorted(table):
        if only and only not in asset:
            continue
        spec = table[asset]
        if is_login_asset(asset):
            log("%s: папка Login — пропуск (окна логина не трогать), узлов %d" % (asset, len(spec["nodes"])))
            continue
        if login_package_blocked(asset, "asda_ui_label_band.py"):
            log("%s: окно сцены логина — пропуск (защита логина), узлов %d" % (asset, len(spec["nodes"])))
            continue
        if not EAL.does_asset_exist(asset):
            inspect.append("%s: ассета нет" % asset)
            continue
        bp = EAL.load_asset(asset)
        root = B.get_root_canvas(bp) if bp else None
        if not root:
            raise RuntimeError("нет корневого канваса у %s" % asset)
        nodes = {}
        for w in T._walk(root):
            nodes.setdefault(w.get_name(), w)
        verdicts = {n: _verdict(n, e, nodes.get(n)) for n, e in spec["nodes"].items()}
        short = asset.rsplit("/", 1)[-1]
        todo = sorted(n for n, (v, _m) in verdicts.items() if v == "fixed")
        okn = sum(1 for v, _m in verdicts.values() if v == "ok")
        fon = sum(1 for v, _m in verdicts.values() if v == "foreign")
        bad = ["%s: %s" % (short, m) for v, m in verdicts.values() if v == "inspect"]
        log("%s (%s): узлов %d — править %d, готово %d, чужой шаг %d, осмотреть %d"
            % (short, spec["window"], len(verdicts), len(todo), okn, fon, len(bad)))
        for n in todo:
            log("   [править] %s" % verdicts[n][1])
        for n, (v, m) in sorted(verdicts.items()):
            if v == "foreign":
                log("   [чужой шаг] %s" % m)
        ok_total += okn
        foreign_total += fon
        if bad:
            inspect += bad
            continue                     # окно с чужим видом хоть одного узла не пишется целиком
        if dry or not todo:
            continue
        if standalone:
            T._ensure_backup(asset, BACKUP_SET, set_dir=BACKUP_DIR_OVERRIDE)
        before = T._snapshot(root)
        for n in todo:
            _apply(spec["nodes"][n], nodes[n])
        after = T._snapshot(B.get_root_canvas(bp))
        lost = [n for n in before if n not in after]
        extra = [n for n in after if n not in before]
        changed = set(todo)
        foreign = [n for n in before if n in after and before[n] != after[n] and n not in changed]
        broken = []
        for n in changed:
            b, a = before[n], after[n]
            # неизменно: класс, родитель, видимость, непрозрачность, кисть, текст; у слота — L, T, W, alignment,
            #   авторазмер, z (анкоры — только если «стало» их меняет)
            if b[:2] != a[:2] or b[3:] != a[3:] or b[2][0][:3] != a[2][0][:3] or b[2][2:] != a[2][2:]:
                broken.append(n)
            elif b[2][1] != a[2][1] and _near(spec["nodes"][n]["before"][1], spec["nodes"][n]["after"][1]):
                broken.append(n)
        log("%s: узлов до %d, после %d; правлено %d; потеряно узлов: %d; лишних: %d; изменено чужих: %d; "
            "испорчено у правленых: %d" % (short, len(before), len(after), len(changed), len(lost), len(extra),
                                          len(foreign), len(broken)))
        if lost or extra or foreign or broken:
            raise RuntimeError("сверка не прошла: потеряно %s, лишних %s, чужих %s, испорчено %s — НЕ сохраняю"
                               % (lost, extra, foreign, broken))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не удалось сохранить %s" % asset)
        log("ASDA_WRITES_ASSET: %s" % asset)
        fixed_total += len(changed)
    for s in inspect:
        log("осмотреть: %s" % s)
    log("окон %d: исправлено узлов %d, уже по низу контрола %d, чужой шаг %d, осмотреть %d%s"
        % (len(table), fixed_total, ok_total, foreign_total, len(inspect), " (СУХОЙ ПРОГОН)" if dry else ""))
    if inspect and not dry:
        raise RuntimeError("полоса-подписи: %d узлов «осмотреть» — вид не совпал с таблицей, см. лог" % len(inspect))
    log("ПОЛОСА-ПОДПИСИ: ГОТОВО")


def _cli():
    """Вне редактора: --plan [--write] / --scan."""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if "--plan" in sys.argv or "--scan" in sys.argv:
        table, scan = build_plan()
        if "--scan" in sys.argv:
            print("ассет\tокно\tузел\tвыход за низ рамки было, px\tстало, px")
            for p, win, n, ob, on in sorted(scan, key=lambda r: (-r[3], r[0], r[2])):
                print("%s\t%s\t%s\t%.1f\t%.1f" % (p.rsplit("/", 1)[-1], win, n, ob, on))
        print("окон %d, узлов %d" % (len(table), sum(len(s["nodes"]) for s in table.values())))
        if "--write" in sys.argv:
            with open(DATA, "w", encoding="utf-8", newline="\n") as f:
                json.dump(table, f, ensure_ascii=False, indent=1, sort_keys=True)
            print("записано:", DATA)
        return
    with open(DATA, encoding="utf-8") as f:
        for a, s in sorted(json.load(f).items()):
            print("%s %s: узлов %d" % (a.rsplit("/", 1)[-1], s["window"], len(s["nodes"])))


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if unreal is None:
        _cli()
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[полоса-подписи] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
