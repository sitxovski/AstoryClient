# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_scroll_geometry_fix.py — ★★★ ГЕОМЕТРИЯ ПОЛОС ПРОКРУТКИ И ЗОН ПРОКРУТКИ ВО ВСЕХ СОБРАННЫХ ОКНАХ (19.09.2026).
#   Та же ошибка импорта, что уже исправлена в окне настроек шагом `asda_ui_option_layout_fix.py`,
#   но для остальных окон. Точечная запись свойств перечисленных узлов — НЕ перегенерация (законы
#   `owner-asset-edits-never-lose`, `no-ui-regeneration-without-permission`, `no-runtime-widget-fixes`).
#
# ═══ 1. ПОЛОСЫ ПРОКРУТКИ (скины `Type = "ScrollBar"`) ═════════════════════════════════════════════
#   Импортёр раскладывал 4 плана ПРОПОРЦИОНАЛЬНО шаблону скина — стрелки и бегунок растягивались.
#   Эталон `uiScrollbarLayoutPlanes@0x12FE500` (слот +0x14 вtable `CScrollBar` 0x150D118) тянет ТОЛЬКО
#   жёлоб (план 0), стрелку «+» (план 2) сдвигает к концу, стрелка «−» (план 1) и бегунок (план 3) — свой
#   размер. Ось задаёт байт +609: загрузчик скина (`ui_core.c:120346`, конец разбора) зовёт слот +4
#   вtable = `sub_12FE410`, а тот делает `+609 = (ScrollBarType из карты свойств != 0)`; ветка
#   `+609 != 0` раскладки работает по ВЫСОТЕ (вертикальная), `+609 == 0` — по ШИРИНЕ (горизонтальная).
#   Все 22 контрола-полосы в `.JUI` — скины со `ScrollBarType = "0"` (горизонтальные).
#   Раскладка — общая функция `asda_jui_parse.scrollbar_plane_rects` (её же зовёт импортёр).
#
# ═══ 2. ЗОНЫ ПРОКРУТКИ (скины `Type = "WheelWindow"`) С ДЕТЬМИ ════════════════════════════════════
#   Импортёр строил группу зоны как ОБЪЕДИНЕНИЕ потомков и не обрезал её — прокручиваемое содержимое
#   торчало за рамку. Исправленный импортёр: группа зоны = СВОЙ прямоугольник контрола + ClipToBounds;
#   от этого сужаются и группы-предки. Здесь пересчитываются обе раскладки (старая и новая) по данным
#   `.JUI`, и в ассет пишутся только разницы: смещения групп, сдвиг прямых детей группы, у которой
#   сменилось начало (экранное место детей не меняется), и обрезка у групп зон.
#
# ═══ ЗАЩИТА ПРАВОК ВЛАДЕЛЬЦА ════════════════════════════════════════════════════════════════════
#   Каждый набор правок (4 плана полосы; цепочка групп зоны) применяется ТОЛЬКО если текущие значения
#   узлов совпадают со «старой» раскладкой импортёра. Иначе узел мог править владелец — набор
#   НЕ трогается и уходит в список «осмотреть». Уже исправленный набор (текущее = новое) — пропуск.
#   Перед сохранением: (а) бэкап ассета снимается шагом в `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_scroll_fix\` со сверкой md5
#   (`asda_headless_tool.backup_file`, ★ Т3 11.10.2026; копия уже есть — исходник до первой правки, не трогается); (б) полный дамп до/после: изменились ровно
#   перечисленные узлы и только поле `off(...)`, обрезка проверяется чтением обратно.
#
# ОКНО НАСТРОЕК `WBP_SYSTEM_Option_NEW` исключено (его ведёт `asda_ui_option_layout_fix.py`).
# ОКНА ЛОГИНА: в списке нет ни одного (все 22 полосы и 3 зоны с детьми — класс gamemain); пакет
#   `/Game/AsdaAssets/UI/Login` дополнительно отсекается явной проверкой.
#
# Запуск (редактор ЗАКРЫТ):
#   python Tools\run_headless_safe.py Tools\asda_ui_scroll_geometry_fix.py --marker "ПРОКРУТКА: ГОТОВО"
#   ASDA_SCROLLFIX_DRYRUN=1 — сухой прогон; ASDA_SCROLLFIX_ONLY=WBP_A,WBP_B — только эти окна.
#   Вне редактора: `python asda_ui_scroll_geometry_fix.py --plan` — печать поимённого плана.
import os
import re
import sys
import traceback

try:
    import unreal
except ImportError:
    unreal = None

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_jui_parse as J  # noqa: E402
import asda_headless_tool as HT  # noqa: E402  ★ Т3 (11.10.2026): единая резервная копия `backup_file`

EPS = 0.05
INI = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui\gamemain_loc.ini"      # тот же файл, что берёт импортёр (`_ini_of`)
PROJECT_CONTENT = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content"
BACKUP_SET = "scroll_fix"   # ★ Т3: набор `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_scroll_fix` (была прошитая папка 2026-09-19_scroll_fix)
AREA_ROOT = r"@@ASTORY_DEV_ROOT@@"

# Окна (корни класса gamemain) → ассет. Список — результат перечня по данным (см. `plan_all`).
ASSETS = {
    "Town_as_WIN":     "/Game/AsdaAssets/UI/Player_UI/CampWar/WBP_Town_as_WIN",
    "SYSTEM_WIN":      "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_WIN",
    "System_3d_setup": "/Game/AsdaAssets/UI/Player_UI/System/WBP_System_3d_setup",
    "COMMUNITY_WIN":   "/Game/AsdaAssets/UI/Player_UI/Social/WBP_COMMUNITY_WIN",
    "CHAT_NEW":        "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW",
    "ETC_NEW_WIN":     "/Game/AsdaAssets/UI/Player_UI/System/WBP_ETC_NEW_WIN",
    "SKILL_WIN":       "/Game/AsdaAssets/UI/Player_UI/Character/WBP_SKILL_WIN",
}
EXCLUDED = {"SYSTEM_Option_NEW": "окно настроек — ведёт asda_ui_option_layout_fix.py"}
EXCLUDED_PATHS = {"SYSTEM_Option_NEW": "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW"}


def san(s):
    return re.sub(r"[^A-Za-z0-9_]", "_", (s or "").strip()) or "win"


# ───────────────────────────── ЧИСТЫЙ РАСЧЁТ (без unreal) ─────────────────────────────
def _skins():
    return J.parse_skins_full()


def _stype(sk, n):
    return (sk.get((n.name or "").lower()) or {}).get("type", "")


def _bounds(sk, n, new):
    """`_subtree_bounds` импортёра: new=False — как до 19.09 (union всегда), new=True — зона = свой rect."""
    l, t, w, h = n.abs
    a = [l, t, l + w, t + h]
    if new and _stype(sk, n) == "WheelWindow":
        return tuple(a)
    for c in n.children:
        b = _bounds(sk, c, new)
        a = [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]
    return tuple(a)


def _anchor_tf(node, x, y, w, h):
    """`_apply_anchor` импортёра для точечного анкора; тянущийся анкор → None (такой узел не правим)."""
    a = node.anchor
    if a["xspan"] or a["yspan"]:
        return None
    return (x - a["fx"] * 1024.0, y - a["fy"] * 768.0, w, h)


def plan_window(root, sk):
    """Наборы правок окна. Набор = {kind, label, edits:[(имя, 'offsets'|'clipping', было, стало)],
    shift:[(имя группы, dx, dy)]} — все правки набора применяются вместе или не применяются вовсе."""
    J.compute_anchors(root)
    sets = []
    idx = [0]
    grp_sets = {}          # набор зон окна (одна цепочка на окно — их объединяем)

    def walk(n, is_top, is_root, pgo_old, pgo_new):
        my = idx[0]
        idx[0] += 1
        st = _stype(sk, n)
        l, t, w, h = n.abs
        own_old = own_new = None
        if (not is_root) and n.children:
            bo, bn = _bounds(sk, n, False), _bounds(sk, n, True)
            own_old, own_new = bo[:2], bn[:2]
            so = (bo[0] - pgo_old[0], bo[1] - pgo_old[1], bo[2] - bo[0], bo[3] - bo[1])
            sn = (bn[0] - pgo_new[0], bn[1] - pgo_new[1], bn[2] - bn[0], bn[3] - bn[1])
            if is_top:
                so, sn = _anchor_tf(n, *so), _anchor_tf(n, *sn)
            viewport = st == "WheelWindow"
            if viewport or so != sn:
                g = grp_sets.setdefault("chain", {"kind": "зона", "label": [], "edits": [], "shift": []})
                gname = san(n.idc or n.name) + "_grp"
                if so is None or sn is None:
                    g["label"].append("%s: тянущийся анкор — не поддержан" % gname)
                    g["bad"] = True
                elif any(abs(a - b) > 1e-6 for a, b in zip(so, sn)):
                    g["edits"].append((gname, "offsets", so, sn))
                if viewport:
                    g["label"].append("%s (IDNum %d, %s)" % (gname, n.idnum, n.name))
                    g["edits"].append((gname, "clipping", "INHERIT", "CLIP_TO_BOUNDS"))
                d = (own_new[0] - own_old[0], own_new[1] - own_old[1])
                if abs(d[0]) > 1e-6 or abs(d[1]) > 1e-6:
                    g["shift"].append((gname, d[0], d[1]))
        if st == "ScrollBar":
            s = sk[(n.name or "").lower()]
            planes = [p for p in s["planes"] if p["tex"] and not p["invisible"] and p.get("fillmode", 0) != 5]
            new = J.scrollbar_plane_rects(planes, w, h, s.get("scrollbar_type", 1) != 0)
            old = J.plane_screen_rects(planes, w, h)
            base = "%s_%d" % (san(n.idc or n.name), my)
            sb = {"kind": "полоса", "label": "%s (IDNum %d, скин %s, ScrollBarType %s, %gx%g)"
                  % (base, n.idnum, n.name, s.get("scrollbar_type"), w, h), "edits": [], "shift": []}
            if new is None:
                sb["bad"] = True
                sb["label"] += ": раскладка эталона неприменима (не 4 плана с Rect)"
            else:
                ox, oy = (own_old if n.children else pgo_old)
                oxn, oyn = (own_new if n.children else pgo_new)
                for j in range(len(planes)):
                    ro, rn = old[j], new[j]
                    so = (l - ox + ro[0], t - oy + ro[1], ro[2], ro[3])
                    sn = (l - oxn + rn[0], t - oyn + rn[1], rn[2], rn[3])
                    if is_top and not n.children:
                        so, sn = _anchor_tf(n, *so), _anchor_tf(n, *sn)
                        if so is None:
                            sb["bad"] = True
                            continue
                    sb["edits"].append(("%s_%d" % (base, j), "offsets", tuple(round(v, 4) for v in so),
                                        tuple(round(v, 4) for v in sn)))
            sets.append(sb)
        cgo_old = own_old if own_old else pgo_old
        cgo_new = own_new if own_new else pgo_new
        for c in n.children:
            walk(c, bool(is_root), False, cgo_old, cgo_new)

    walk(root, True, True, (0.0, 0.0), (0.0, 0.0))
    if "chain" in grp_sets:
        g = grp_sets["chain"]
        g["label"] = "; ".join(g["label"])
        sets.append(g)
    return sets


def plan_all():
    """{ключ окна: [наборы]} по всем корням gamemain, где есть полосы или зоны с детьми."""
    sk = _skins()
    out = {}
    for r in J.parse_jui(INI):
        key = san(r.idc or r.name)
        sets = plan_window(r, sk)
        sets = [s for s in sets if s["edits"] or s.get("bad")]
        if sets:
            out[key] = sets
    return out


def print_plan():
    pl = plan_all()
    for key, sets in pl.items():
        tag = " [ИСКЛЮЧЕНО: %s]" % EXCLUDED[key] if key in EXCLUDED else ""
        print("══ %s → %s%s" % (key, ASSETS.get(key, "НЕТ В СПИСКЕ АССЕТОВ"), tag))
        for s in sets:
            print("  [%s] %s%s" % (s["kind"], s["label"], "  ⚠ НЕ ПРИМЕНИМ" if s.get("bad") else ""))
            for nm, f, a, b in s["edits"]:
                if f == "offsets":
                    print("     %-34s off (%.2f, %.2f, %.2f, %.2f) → (%.2f, %.2f, %.2f, %.2f)" % ((nm,) + tuple(a) + tuple(b)))
                else:
                    print("     %-34s %s %s → %s" % (nm, f, a, b))
            for nm, dx, dy in s["shift"]:
                print("     прямые дети %s (кроме групп из списка): смещение на (%.2f, %.2f)" % (nm, -dx, -dy))


# ───────────────────────────── ЗАПИСЬ В АССЕТ (редактор) ─────────────────────────────
def log(m):
    unreal.log("[прокрутка] %s" % m)


def _walk_widgets(root):
    out = {}
    st = [root]
    while st:
        w = st.pop()
        if w is None:
            continue
        out.setdefault(w.get_name(), []).append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def _dump_map(bp):
    out = {}
    for raw in unreal.AsdaUmgBuilder.dump_widget_tree(bp).split("\n"):
        line = raw.strip()
        if not line or line.startswith("==="):
            continue
        parts = line.split(None, 1)
        if len(parts) == 2:
            out[parts[0]] = " ".join(parts[1].split())
    return out


def _off(w):
    m = w.get_editor_property("slot").get_editor_property("layout_data").get_editor_property("offsets")
    return (float(m.left), float(m.top), float(m.right), float(m.bottom))


def _anch_point(w):
    a = w.get_editor_property("slot").get_editor_property("layout_data").get_editor_property("anchors")
    mn, mx = a.get_editor_property("minimum"), a.get_editor_property("maximum")
    return abs(mn.x - mx.x) < 1e-4 and abs(mn.y - mx.y) < 1e-4


def _set_off(w, m):
    s = w.get_editor_property("slot")
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(m[0], m[1], m[2], m[3]))
    s.set_editor_property("layout_data", lay)


def _same(a, b):
    return all(abs(x - y) < EPS for x, y in zip(a, b))


def _disk_file(asset):
    return os.path.join(PROJECT_CONTENT, asset[len("/Game/"):].replace("/", os.sep) + ".uasset")


def _backup_ok(asset):
    """Бэкап ДО записи — общая `asda_headless_tool.backup_file(verify="md5")` (★ Т3, 11.10.2026). Возврат (ok, путь|причина).

    ★ 19.09.2026 (ревизия, СРЕДН): прежде сверялся только РАЗМЕР. Для большинства окон запись не
    меняет размер файла, поэтому бэкап, снятый ПОСЛЕ записи, такую проверку прошёл бы — а на этой
    неделе бэкап дважды снимали после записи. Тогда ввели md5: копия обязана совпадать с файлом ДО записи.
    ★ Т3 (11.10.2026): копия больше не обязана лежать заранее в прошитой папке `2026-09-19_scroll_fix` — шаг сам снимает
    её в набор `<дата запуска>_scroll_fix` ПРЯМО ПЕРЕД записью и сверяет md5 копии с файлом (копия по построению = состояние
    до записи; `copy2` сохраняет время файла — копия не новее исходника). Копия набора уже есть (повторный запуск в тот
    же день) — это исходник до ПЕРВОЙ правки: не перезаписывается, запись не запрещается, в журнал — «файл уже изменён
    после бэкапа» при несовпадении md5. Отказ — только если копию снять/сверить не удалось или копия пустая.
    """
    src = _disk_file(asset)
    try:
        bak = HT.backup_file(src, BACKUP_SET, verify="md5", log=log)
    except (OSError, RuntimeError) as e:
        return False, "бэкап не снят: %s" % e
    if bak is None:
        return False, "нет файла ассета %s" % src
    return True, bak


def _check_set(s, nodes):
    """(ok|'done'|'inspect', причина). ok — текущее = старая раскладка у ВСЕХ узлов набора."""
    if s.get("bad"):
        return "inspect", "набор неприменим по данным"
    n_old = n_new = 0
    for nm, f, was, want in s["edits"]:
        ws = nodes.get(nm) or []
        if len(ws) != 1:
            return "inspect", "узел %s: найдено %d" % (nm, len(ws))
        w = ws[0]
        if f == "offsets":
            if not isinstance(w.get_editor_property("slot"), unreal.CanvasPanelSlot) or not _anch_point(w):
                return "inspect", "узел %s: не точечный анкор CanvasPanel" % nm
            cur = _off(w)
            if _same(was, want):
                # Узел, у которого старая и новая раскладки совпадают (напр. жёлоб полосы шириной во весь
                #   контрол), о состоянии набора не говорит ничего — только проверяем, что он не тронут.
                if not _same(cur, want):
                    return "inspect", "узел %s: off %s ≠ ожидаемому %s (ручная правка?)" % (
                        nm, tuple(round(v, 2) for v in cur), tuple(round(v, 2) for v in want))
                continue
            if _same(cur, want):
                n_new += 1
            elif _same(cur, was):
                n_old += 1
            else:
                return "inspect", "узел %s: off %s ≠ старой раскладке %s и ≠ новой %s (ручная правка?)" % (
                    nm, tuple(round(v, 2) for v in cur), tuple(round(v, 2) for v in was),
                    tuple(round(v, 2) for v in want))
        else:
            clip = w.get_editor_property("clipping")
            if clip == unreal.WidgetClipping.CLIP_TO_BOUNDS:
                n_new += 1
            elif clip == unreal.WidgetClipping.INHERIT:
                n_old += 1
            else:
                return "inspect", "узел %s: обрезка %s (не Inherit)" % (nm, clip)
    for nm, dx, dy in s["shift"]:
        if len(nodes.get(nm) or []) != 1:
            return "inspect", "группа сдвига %s не найдена однозначно" % nm
    if n_old == 0:
        return "done", "уже исправлено"
    if n_new:
        return "inspect", "набор исправлен частично (%d старых, %d новых)" % (n_old, n_new)
    return "ok", ""


def fix_asset(key, asset, sets, dry, report):
    if "/UI/Login" in asset:
        report["login"].append(asset)
        log("%s: пакет логина — НЕ ТРОГАЮ" % asset)
        return
    bp = unreal.EditorAssetLibrary.load_asset(asset)
    if not bp:
        raise RuntimeError("не загрузился %s" % asset)
    before = _dump_map(bp)
    nodes = _walk_widgets(unreal.AsdaUmgBuilder.get_root_canvas(bp))
    todo = []
    for s in sets:
        verdict, why = _check_set(s, nodes)
        if verdict == "ok":
            todo.append(s)
        elif verdict == "done":
            log("%s [%s] %s — %s" % (key, s["kind"], s["label"], why))
        else:
            report["inspect"].append("%s [%s] %s — %s" % (key, s["kind"], s["label"], why))
            log("⚠ ОСМОТРЕТЬ %s [%s] %s — %s" % (key, s["kind"], s["label"], why))
    if not todo:
        log("%s: правок нет" % key)
        return
    # Полный список правок: явные + сдвиг прямых детей групп, у которых сменилось начало.
    explicit = set(nm for s in todo for nm, f, a, b in s["edits"] if f == "offsets")
    edits = []
    for s in todo:
        for nm, f, was, want in s["edits"]:
            if f == "offsets" and _same(was, want):
                continue          # раскладка узла не меняется — не пишем
            edits.append((nodes[nm][0], f, was, want))
        for gname, dx, dy in s["shift"]:
            g = nodes[gname][0]
            for i in range(g.get_children_count()):
                k = g.get_child_at(i)
                if k.get_name() in explicit:
                    continue
                cur = _off(k)
                edits.append((k, "offsets", cur, (cur[0] - dx, cur[1] - dy, cur[2], cur[3])))
    for w, f, was, want in edits:
        log("%s%s: %s.%s: %s → %s" % ("[сухой] " if dry else "", key, w.get_name(), f,
                                      tuple(round(v, 2) for v in was) if f == "offsets" else was,
                                      tuple(round(v, 2) for v in want) if f == "offsets" else want))
    if dry:
        report["planned"] += len(edits)
        return
    ok, why = _backup_ok(asset)
    if not ok:
        raise RuntimeError("%s: БЭКАПА ДО ЗАПИСИ НЕТ — не пишу (%s)" % (asset, why))
    log("%s: бэкап на месте — %s" % (key, why))
    clip_nodes = []
    for w, f, was, want in edits:
        if f == "offsets":
            _set_off(w, want)
        else:
            w.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
            clip_nodes.append(w)
    after = _dump_map(bp)
    touched = set(w.get_name() for w, f, a, b in edits if f == "offsets")
    bad = []
    off_rx = re.compile(r"off\([^)]*\)")
    for n in before:
        if n not in after:
            bad.append("пропал %s" % n)
            continue
        if before[n] == after[n]:
            continue
        if n not in touched:
            bad.append("изменён неперечисленный %s" % n)
        elif off_rx.sub("", before[n]) != off_rx.sub("", after[n]):
            bad.append("у %s изменилось не только смещение" % n)
    if set(after) - set(before):
        bad.append("появились узлы %s" % sorted(set(after) - set(before)))
    for w in clip_nodes:
        if w.get_editor_property("clipping") != unreal.WidgetClipping.CLIP_TO_BOUNDS:
            bad.append("у %s не встала обрезка" % w.get_name())
    for w, f, was, want in edits:
        if f == "offsets" and not _same(_off(w), want):
            bad.append("у %s не встало смещение" % w.get_name())
    if bad:
        raise RuntimeError("%s: СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % (asset, "; ".join(bad)))
    unreal.AsdaUmgBuilder.compile_and_save(bp)
    # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp, False):
        raise RuntimeError("не сохранён %s" % asset)
    changed = sorted(n for n in before if before[n] != after[n])
    report["written"].append((key, len(edits), len(clip_nodes)))
    log("%s: СВЕРКА ОК — изменены ровно %d узлов (только смещения) + обрезка у %s; прочие %d узлов нетронуты; "
        "изменённые: %s" % (key, len(changed), [w.get_name() for w in clip_nodes], len(before) - len(changed),
                            ", ".join(changed)))


def main():
    dry = bool(os.environ.get("ASDA_SCROLLFIX_DRYRUN"))
    only = [x.strip() for x in os.environ.get("ASDA_SCROLLFIX_ONLY", "").split(",") if x.strip()]
    pl = plan_all()
    report = {"inspect": [], "login": [], "written": [], "planned": 0}
    for key, sets in pl.items():
        if key in EXCLUDED:
            log("%s — ИСКЛЮЧЕНО: %s" % (key, EXCLUDED[key]))
            # Только чтение: состояние исключённого окна по тем же наборам (для отчёта, без записи).
            path = EXCLUDED_PATHS.get(key)
            bp = unreal.EditorAssetLibrary.load_asset(path) if path else None
            if bp:
                nodes = _walk_widgets(unreal.AsdaUmgBuilder.get_root_canvas(bp))
                for s in sets:
                    verdict, why = _check_set(s, nodes)
                    log("  [только чтение] %s [%s] %s — %s %s" % (key, s["kind"], s["label"], verdict, why))
            continue
        asset = ASSETS.get(key)
        if not asset:
            raise RuntimeError("окно %s есть в перечне, но не в списке ASSETS — перечень устарел" % key)
        if only and key not in only and asset.rsplit("/", 1)[-1] not in only:
            continue
        fix_asset(key, asset, sets, dry, report)
    for k in ASSETS:
        if k not in pl:
            log("⚠ окно %s в списке ASSETS, но правок по данным нет" % k)
    log("ИТОГ: записано окон %d %s; осмотреть %d; окна логина %s; сухих правок %d"
        % (len(report["written"]), report["written"], len(report["inspect"]), report["login"], report["planned"]))
    for s in report["inspect"]:
        log("  ОСМОТРЕТЬ: %s" % s)
    log("ПРОКРУТКА: ГОТОВО%s" % ("  [СУХОЙ ПРОГОН]" if dry else ""))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    if unreal is None or "--plan" in sys.argv:
        try:
            sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
        print_plan()
    else:
        try:
            main()
        except Exception:
            unreal.log_error("[прокрутка] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
