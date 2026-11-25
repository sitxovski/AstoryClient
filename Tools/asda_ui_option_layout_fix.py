# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_option_layout_fix.py — ★★★ ГЕОМЕТРИЯ ДВУХ КОНТРОЛОВ ОКНА НАСТРОЕК 69500 В АССЕТЕ
#   (`WBP_SYSTEM_Option_NEW`): полосы громкости и зона прокрутки страницы клавиш.
#   Точечная запись свойств перечисленных узлов — не перегенерация (законы
#   `owner-asset-edits-never-lose`, `no-ui-regeneration-without-permission`, `no-runtime-widget-fixes`).
#
# ═══ 1. ПОЛОСЫ ГРОМКОСТИ 70013 / 70016 И ЯРКОСТИ 70058 (жалоба: «растянутые и масштабированные») ═══════
#   Скин `SideBar_Scr` (`JuiSkin.ini`, `Type = "ScrollBar"`, `ScrollBarType = "0"` — горизонтальная)
#   задан шаблоном 76×11: жёлоб 13…63, стрелка «−» 0…11, стрелка «+» 65…76, бегунок 22…33. Контрол —
#   136×11. Импортёр раскладывал планы ПРОПОРЦИОНАЛЬНО (×1.79), и стрелки с бегунком вышли 19.7×11 —
#   сплюснутые эллипсы. Эталон `uiScrollbarLayoutPlanes@0x12FE500` тянет ТОЛЬКО жёлоб: сдвиг = 136 − 76 = 60,
#   жёлоб 13…123, «+» 125…136, «−» 0…11, бегунок 11 px (позицию ставит `uiScrollbarLayoutThumb`).
#   Раскладка считается общей функцией `asda_jui_parse.scrollbar_plane_rects` (ею же теперь пользуется
#   импортёр), шаблон — из `JuiSkin.ini`, ничего не задано руками.
#
# ═══ 2. ЗОНА ПРОКРУТКИ СТРАНИЦЫ КЛАВИШ 69649 (жалоба: «список вылезает за рамку вниз и вверх») ═══
#   Данные `.JUI`: `SYSTEM_Op_HOTKEYS` (69510) 681×450; внутри `SYSTEM_Op_HOTKEYS_Scroll` (69649, скин
#   `WheelWin_Nomal`, `Type = "WheelWindow"`) — Origin 178,8, Size 484×346; его дети — четыре страницы
#   (69648/69650/69750/69850) с Origin −2,−4. Импортёр строил группу как ОБЪЕДИНЕНИЕ потомков: группа
#   зоны вышла 176,4 / 486×655, контейнер вкладки 681×659, и НИКТО не обрезал содержимое — список
#   стоял поверх рамки окна и кнопок «По умолчанию»/«ОК». Лечение по данным:
#     • `SYSTEM_Op_HOTKEYS_grp` — высота 659 → 450 (Size контрола 69510);
#     • `SYSTEM_Op_HOTKEYS_Scroll_grp` — 176,4 / 486×655 → 178,8 / 484×346 (Origin/Size 69649) и
#       обрезка `Clipping = ClipToBounds` (зона прокрутки показывает только своё окно);
#     • прямые дети этой группы сдвигаются на (−2, −4), чтобы их ЭКРАННОЕ место не изменилось
#       (страницы встают в Origin −2,−4 из данных, зона колеса `…_Scroll_24_HIT` — в 0,0).
#   Прокрутку по-прежнему ведёт код (смещение страницы на 19 px — это состояние, а не разметка).
#
# ═══ ПРОВЕРКА ═══════════════════════════════════════════════════════════════════════════════════
#   Поимённый список «было → стало» печатается до записи. После записи полный дамп сверяется с
#   исходным: измениться обязаны РОВНО перечисленные узлы и ровно в перечисленных полях; иначе —
#   исключение, сохранения нет. Второй прогон = 0 правок.
#
# Запуск (редактор ЗАКРЫТ): python Tools\run_headless_safe.py Tools\asda_ui_option_layout_fix.py
#                              --marker "ГЕОМЕТРИЯ 69500: ГОТОВО"
#   Сухой прогон: set ASDA_OPTLAYOUT_DRYRUN=1
import codecs
import os
import re
import sys
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import asda_jui_parse as J  # noqa: E402
# ★ дедуп №2 В1 Т12: общие помощники правки WBP (обход дерева, канвасный слот, смещения) — asda_ui_widget_common
import os as _uwc_os, sys as _uwc_sys
if _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)) not in _uwc_sys.path:
    _uwc_sys.path.insert(0, _uwc_os.path.dirname(_uwc_os.path.abspath(__file__)))
import asda_ui_widget_common as _UWC   # noqa: E402

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder
EPS = 0.05

ASSET = "/Game/AsdaAssets/UI/Player_UI/System/WBP_SYSTEM_Option_NEW"
JUI_INI = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui\gamemain_jui.ini"

# Узлы планов полос (имена импортёра `SideBar_Scr_<jui>_<план>`; план 0 жёлоб, 1 «−», 2 «+», 3 бегунок).
#   232 — громкость музыки 70013, 235 — громкость эффектов 70016,
#   ★ 242 — ЯРКОСТЬ 70058 (добавлена 19.09.2026, фаза 2 окна настроек): была разложена тем же пропорциональным
#     способом (стрелки и бегунок 19.7×11). Ориентация по эталону: `ScrollBarType` скина → байт +609
#     (`sub_12FE410`; конструктор ставит 1), 0 = горизонтальная — у `SideBar_Scr` 0, раскладка по ширине.
#     Числа — ровно план параллельного исполнителя (`scroll_fix/plan.txt`, та же `scrollbar_plane_rects`):
#     242_0 → 236,3 110×11; 242_1 → 223,3 11×11; 242_2 → 348,3 11×11; 242_3 → 245,3 11×11.
#     Её же клонирует шаг `asda_ui_option_extra_rows.py` для «Дальности прорисовки» — поэтому этот шаг идёт РАНЬШЕ.
SLIDER_JUI = (232, 235, 242)


def log(m):
    unreal.log("[геометрия 69500] %s" % m)


walk = _UWC.walk_by_name   # ★ дедуп №2 В1 Т12: общий помощник (тело перенесено дословно)


def dump_map(bp):
    out = {}
    for raw in B.dump_widget_tree(bp).split("\n"):
        line = raw.strip()
        if not line or line.startswith("==="):
            continue
        parts = line.split(None, 1)
        if len(parts) == 2:
            out[parts[0]] = " ".join(parts[1].split())
    return out


def offsets(w):
    return w.get_editor_property("slot").get_editor_property("layout_data").get_editor_property("offsets")


def set_offsets(w, m):
    s = w.get_editor_property("slot")
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(m[0], m[1], m[2], m[3]))
    s.set_editor_property("layout_data", lay)


def tup(m):
    return (float(m.left), float(m.top), float(m.right), float(m.bottom))


def same(a, b):
    return all(abs(x - y) < EPS for x, y in zip(a, b))


def jui_origin_size(idc):
    """Origin/Size контрола с данным `ID` из `gamemain_jui.ini` (данные, а не догадка)."""
    lines = codecs.open(JUI_INI, "r", encoding="cp949", errors="replace").read().split("\n")
    for i, l in enumerate(lines):
        if l.strip() == 'ID = "%s"' % idc:
            org = size = None
            for k in range(i, min(i + 20, len(lines))):
                m = re.match(r'\s*(Origin|Size) = "(-?\d+) (-?\d+)"', lines[k])
                if m:
                    v = (float(m.group(2)), float(m.group(3)))
                    if m.group(1) == "Origin" and org is None:
                        org = v
                    elif m.group(1) == "Size" and size is None:
                        size = v
                if org and size:
                    return org, size
    raise RuntimeError("в .JUI нет контрола %s" % idc)


def plan(nodes):
    """Список правок: (узел, поле, было, стало)."""
    edits = []
    # ── 1. Полосы громкости ──
    sk = J.parse_skins_full()["sidebar_scr"]
    planes = [p for p in sk["planes"] if p["tex"] and not p["invisible"] and p.get("fillmode", 0) != 5]
    for jui in SLIDER_JUI:
        ws = [nodes.get("SideBar_Scr_%d_%d" % (jui, j)) for j in range(4)]
        if any(w is None for w in ws):
            raise RuntimeError("нет планов полосы SideBar_Scr_%d_*" % jui)
        cur = [tup(offsets(w)) for w in ws]
        # Прямоугольник контрола: левый край = левая стрелка (её левый край в шаблоне 0),
        #   ширина — по данным контрола (136), высота — по данным (11).
        left = min(c[0] for c in cur)
        top = min(c[1] for c in cur)
        W = max(c[0] + c[2] for c in cur) - left
        H = max(c[1] + c[3] for c in cur) - top
        lay = J.scrollbar_plane_rects(planes, W, H, sk.get("scrollbar_type", 1) != 0)
        if lay is None:
            raise RuntimeError("раскладка полосы не посчиталась")
        for j, w in enumerate(ws):
            rx, ry, rw, rh, _p = lay[j]
            # Округление до сотых: `W` собран из float-слотов импортёра (157.0000057 − 21).
            want = tuple(round(v, 2) for v in (left + rx, top + ry, rw, rh))
            if not same(cur[j], want):
                edits.append((w, "offsets", cur[j], want))
    # ── 2. Зона прокрутки страницы клавиш ──
    (_ox, _oy), (tw, th) = jui_origin_size("SYSTEM_Op_HOTKEYS")
    (sx, sy), (sw, sh) = jui_origin_size("SYSTEM_Op_HOTKEYS_Scroll")
    tab = nodes.get("SYSTEM_Op_HOTKEYS_grp")
    scr = nodes.get("SYSTEM_Op_HOTKEYS_Scroll_grp")
    if tab is None or scr is None:
        raise RuntimeError("нет групп вкладки клавиш")
    t_cur = tup(offsets(tab))
    t_want = (t_cur[0], t_cur[1], tw, th)       # позицию вкладки не трогаем (её анкор — окно), только размер
    if not same(t_cur, t_want):
        edits.append((tab, "offsets", t_cur, t_want))
    s_cur = tup(offsets(scr))
    s_want = (sx, sy, sw, sh)
    dx, dy = s_want[0] - s_cur[0], s_want[1] - s_cur[1]
    if not same(s_cur, s_want):
        edits.append((scr, "offsets", s_cur, s_want))
        # Дети — на (−dx, −dy): экранное место содержимого не меняется.
        for i in range(scr.get_children_count()):
            k = scr.get_child_at(i)
            k_cur = tup(offsets(k))
            edits.append((k, "offsets", k_cur, (k_cur[0] - dx, k_cur[1] - dy, k_cur[2], k_cur[3])))
    clip = scr.get_editor_property("clipping")
    if clip != unreal.WidgetClipping.CLIP_TO_BOUNDS:
        edits.append((scr, "clipping", str(clip), unreal.WidgetClipping.CLIP_TO_BOUNDS))
    return edits


def main():
    dry = bool(os.environ.get("ASDA_OPTLAYOUT_DRYRUN"))
    bp = EAL.load_asset(ASSET)
    if not bp:
        raise RuntimeError("не загрузился %s" % ASSET)
    before = dump_map(bp)
    nodes = walk(B.get_root_canvas(bp))
    edits = plan(nodes)
    for w, field, was, want in edits:
        log("%s%s.%s: %s → %s" % ("[сухой] " if dry else "", w.get_name(), field, was, want))
    if edits and not dry:
        clip_nodes = set()
        for w, field, was, want in edits:
            if field == "offsets":
                set_offsets(w, want)
            else:
                w.set_editor_property("clipping", want)
                clip_nodes.add(w.get_name())
        after = dump_map(bp)
        touched = set(w.get_name() for w, f, a, b in edits if f == "offsets")
        # Сверка: изменились ТОЛЬКО перечисленные узлы, и у них — только поле `off(...)`.
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
        # Обрезка в дамп не попадает — проверяем чтением обратно.
        for n in clip_nodes:
            if nodes[n].get_editor_property("clipping") != unreal.WidgetClipping.CLIP_TO_BOUNDS:
                bad.append("у %s не встала обрезка" % n)
        if bad:
            raise RuntimeError("СВЕРКА НЕ ПРОШЛА — не сохраняю: %s" % "; ".join(bad))
        log("сверка: изменены ровно %d перечисленных узлов (только смещения) + обрезка у %s; прочие %d узлов нетронуты"
            % (len(touched), sorted(clip_nodes), len(before) - len(touched)))
        # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
        B.compile_and_save(bp)
        if not EAL.save_loaded_asset(bp, False):
            raise RuntimeError("не сохранён %s" % ASSET)
    log("ГЕОМЕТРИЯ 69500: ГОТОВО%s — правок %d" % ("  [СУХОЙ ПРОГОН]" if dry else "", len(edits)))


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("[геометрия 69500] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
