# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_smelting3_scroll.py — ★★★ ПОЛОСА ПРОКРУТКИ СПИСКА СНАРЯЖЕНИЯ ОКНА ВЫСОКОЙ ПЕРЕПЛАВКИ 78200
#   (`WBP_Smelting3_Win`, окно `Smelting3_Win`). Разрешение владельца 25.09.2026: «Полосу прокрутки
#   добавь. Возьми её из ассетов игры». Отчёт: `Reports/gear_upgrade/wave12_scroll_panel.md`.
#
# ═══ ЧТО ЭТО В ОРИГИНАЛЕ ═════════════════════════════════════════════════════════════════════════
#   78226 `Smelting3_list_1` и 78265 `Smelting3_list_2` — контролы скина `WheelWin_Nomal`
#   (`JuiSkin.ini`: `Type = "WheelWindow"`, `ScrollBarFileNameVert = "Nomal_Scr"`, единственный план
#   `PlaneAlwaysInvisible = 1`), `NoSelect = 1`, `VertScrollPosType = 1`, в `.JUI` оба `Visible = 0`.
#   78226 лежит в рамке списка снаряжения (`Win_Tab`, Origin 10 7, Size 234×66 — ровно сетка 7×2),
#   78265 — в рамке списка материалов (Origin 9 25, Size 164×99 — три строки материалов).
#   Полосу движок создаёт В РАНТАЙМЕ из ключа скина (`uiControl_AttachVertScrollbar@0x12F3160` →
#   `uiControl_attachScrollbar@0x12F35B0`): `CScrollBar` скина `Nomal_Scr` РЕБЁНКОМ колёсного окна,
#   левый-верх = (правый край плана 0, его верх), высота = высота плана 0, ширина — из скина (11).
#   Код окна (`CHighRefineUI_InitControls`, ui_core.c:46041/46071-46072) берёт только 78226
#   («Smelting3_Scroll»): `uiControlSetScrollRange(78226, 1, 2)` + `uiControlSetScrollPage(78226, 1, 1)`;
#   78265 код не трогает вовсе. Оба контрола скрыты — полосы у оригинала на экране нет (дефект Д7-3).
#
# ═══ ЧТО ДЕЛАЕТ ШАГ (точечно, идемпотентно: второй прогон = 0 правок) ═══════════════════════════
#   В группе рамки списка снаряжения (родитель `NoImage_icon_grp` с иконками 78211…) создаёт:
#     • `Smelting3_list_1` — зона колёсного окна 78226 (невидимая, мышь насквозь: `NoSelect = 1`);
#       ВИДИМА по разрешению владельца (в `.JUI` `Visible = 0`) — [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА Д7-3];
#     • `Smelting3_list_1_Scr_Track/_Thumb/_Up/_Down` — четыре плана полосы скина `Nomal_Scr`,
#       ВСЕ ЧИСЛА ИЗ ДАННЫХ: атлас/UV/Diffuse планов — `JuiSkin.ini` (через `asda_jui_parse`), раскладка —
#       общая функция импортёра `scrollbar_plane_rects` (порт `uiScrollbarLayoutPlanes`), место — правый
#       край зоны по `uiControl_attachScrollbar`. Бегунок — в точке шаблона скина (его ставит рантайм,
#       `FAsdaScrollBarNomal::Layout` по странице списка).
#   В группе рамки материалов (родитель `Smelting3_MARK2_1_grp`) создаёт `Smelting3_list_2` — зону
#   78265 СКРЫТОЙ, как в `.JUI` (полосы у неё нет: код оригинала её не касается, материалов всегда ≤ 3).
#
# ═══ ЗАЩИТА ПРАВОК ВЛАДЕЛЬЦА ════════════════════════════════════════════════════════════════════
#   • Существующий узел с тем же именем НЕ трогается (мог править владелец) — только отчёт о расхождении.
#   • До записи: бэкап ассета `asda_headless_tool.backup_file` (★ Т3, 11.10.2026) — снимается шагом, если его нет; есть —
#     исходник до первой правки, не перезаписывается (прежде копия обязана была лежать заранее в прошитой папке).
#   • Полный снимок до/после (имя → класс, родитель, слот, видимость, непрозрачность, а у картинок — кисть:
#     текстура, UV, тинт, draw_as, размер; у текстов — текст и цвет): все прежние узлы на месте и не
#     изменились — иначе исключение ДО сохранения («потеряно узлов: N» / «изменено узлов: N»).
#     ★ Волна 13 (М-4 ревизии волны 12): раньше сверка брала только класс/родителя/слот/видимость, и
#     «изменено 0» говорило больше, чем проверялось. Существующий план полосы сверяется с данными скина
#     тоже по кисти (UV, тинт), а не только по месту.
#
# Запуск (редактор ЗАКРЫТ):
#   python Tools\run_headless_safe.py Tools\asda_ui_smelting3_scroll.py --marker "ПОЛОСА 78200: ГОТОВО"
#   ASDA_SMELT3_DRYRUN=1 — сухой прогон (ничего не пишет). Вне редактора: `python asda_ui_smelting3_scroll.py --plan`.
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
import asda_jui_parse as J  # noqa: E402
import asda_headless_tool as HT  # noqa: E402  ★ Т3 (11.10.2026): единая резервная копия `backup_file`

# Ассет окна и его файл на диске (для проверки бэкапа).
ASSET = "/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Smelting3_Win"
ASSET_FILE = r"@@ASTORY_DEV_ROOT@@\AstoryClient\Content\AsdaAssets\UI\Player_UI\Craft\WBP_Smelting3_Win.uasset"
# Бэкап ДО записи (закон backup-before-edit; ★ Т3, 11.10.2026): набор `@@ASTORY_DEV_ROOT@@\_backup\<дата запуска>_smelting3_scroll\
#   <путь от @@ASTORY_DEV_ROOT@@>` (была прошитая папка `2026-09-25_smelting3_scroll`).
BACKUP_SET = "smelting3_scroll"
AREA_ROOT = r"@@ASTORY_DEV_ROOT@@"
# Файлы данных — те же, что читает импортёр.
INI = r"@@ASTORY_DEV_ROOT@@\Data\Decoded\jui\gamemain_loc.ini"
SKIN_INI = r"@@ASTORY_DEV_ROOT@@\RE\docs\JuiSkin.ini"
# Атлас скина `Nomal_Scr` (`N_01.tga`) — тот же путь, что `AsdaNomalScr::kAtlasPath` в коде порта.
ATLAS = "/Game/AsdaAssets/Textures/JUI/n_01.n_01"

# Колёсные окна: IDNum → (якорь-узел для поиска группы рамки, строить ли полосу, видима ли зона).
#   Якорь — узел, который импортёр кладёт ПРЯМО в группу рамки `Win_Tab` соответствующего списка.
ZONES = {
    78226: ("NoImage_icon_grp", True, True),         # список снаряжения: полоса есть, зона видима (владелец)
    78265: ("Smelting3_MARK2_1_grp", False, False),  # список материалов: как в .JUI — скрыта, без полосы
}
# Порядок и имена четырёх планов полосы (индекс плана скина → суффикс узла).
PLANE_SUFFIX = {0: "_Scr_Track", 3: "_Scr_Thumb", 1: "_Scr_Up", 2: "_Scr_Down"}
PLANE_ORDER = (0, 3, 1, 2)   # порядок отрисовки как у `FAsdaScrollBarNomal`: дорожка → бегунок → стрелки
EPS = 0.05


def log(m):
    """Строка журнала шага (в редакторе — в лог UE, вне — в stdout)."""
    if unreal:
        unreal.log("[полоса 78200] %s" % m)
    else:
        print("[полоса 78200] %s" % m)


# ───────────────────────────── ЧИСТЫЙ РАСЧЁТ (без unreal) ─────────────────────────────
def _wheel_scroll_skin(wheel_skin):
    """Значение `ScrollBarFileNameVert` колёсного скина из `JuiSkin.ini` (парсер его не хранит)."""
    import codecs
    cur = None
    for raw in codecs.open(SKIN_INI, "r", encoding="cp1251", errors="replace"):
        s = raw.strip()
        if s.startswith("Name") and "=" in s:
            cur = s.split("=", 1)[1].strip().strip('"')
        elif cur == wheel_skin and s.startswith("ScrollBarFileNameVert"):
            return s.split("=", 1)[1].strip().strip('"')
    return ""


def plan():
    """План по данным: для каждой зоны — геометрия относительно рамки `Win_Tab`, скин полосы и её планы.
    Возврат: {idnum: {"idc", "origin", "size", "visible_jui", "noselect", "scr_skin", "planes": [(j, rx, ry, rw, rh, plane)]}}."""
    roots = J.parse_jui(INI)
    skins = J.parse_skins_full()
    out = {}
    for idn in ZONES:
        n = J.find_by_idnum(roots, idn)
        if n is None:
            raise RuntimeError("в %s нет контрола %d" % (INI, idn))
        sk = skins.get((n.name or "").lower()) or {}
        if sk.get("type") != "WheelWindow":
            raise RuntimeError("контрол %d: скин %s не WheelWindow (%s)" % (idn, n.name, sk.get("type")))
        scr_name = _wheel_scroll_skin(n.name)
        scr = skins.get(scr_name.lower()) or {}
        if scr.get("type") != "ScrollBar" or len(scr.get("planes") or []) != 4:
            raise RuntimeError("контрол %d: скин полосы %r не ScrollBar из 4 планов" % (idn, scr_name))
        w, h = float(n.size[0]), float(n.size[1])
        # `uiControl_attachScrollbar`: полоса — ребёнок зоны, x = правый край плана 0 зоны, y = его верх,
        #   высота = высота плана 0; ширина полосы = ширина планов скина (Rect 0…11).
        bar_w = max(p["rect"][2] for p in scr["planes"])
        rects = J.scrollbar_plane_rects(scr["planes"], bar_w, h, vertical=int(scr.get("scrollbar_type", 0)) != 0)
        if rects is None:
            raise RuntimeError("скин %s: раскладка полосы не посчитана" % scr_name)
        planes = [(j, w + rx, ry, rw, rh, p) for j, (rx, ry, rw, rh, p) in enumerate(rects)]
        out[idn] = {"idc": n.idc, "origin": (float(n.origin[0]), float(n.origin[1])), "size": (w, h),
                    "visible_jui": n.visible, "noselect": n.noselect, "scr_skin": scr_name, "planes": planes}
    return out


def print_plan():
    """Печать плана вне редактора (`--plan`)."""
    for idn, z in plan().items():
        print("%d %s: origin=%s size=%s Visible(.JUI)=%d NoSelect=%d полоса=%s"
              % (idn, z["idc"], z["origin"], z["size"], z["visible_jui"], z["noselect"], z["scr_skin"]))
        for j, rx, ry, rw, rh, p in z["planes"]:
            print("   план %d: от зоны (%.1f, %.1f) %.1fx%.1f  uv=%s  diffuse=%s  tex=%s"
                  % (j, rx, ry, rw, rh, p["uv"], tuple(round(c, 3) for c in p["diffuse"]), p["tex"]))


# ───────────────────────────── РЕДАКТОР ─────────────────────────────
def _walk(root):
    """Все виджеты дерева (корень включительно)."""
    st, out = [root], []
    while st:
        w = st.pop()
        if not w:
            continue
        out.append(w)
        if isinstance(w, unreal.PanelWidget):
            for i in range(w.get_children_count()):
                st.append(w.get_child_at(i))
    return out


def _slot(w):
    """Слот канваса виджета или None."""
    try:
        s = w.get_editor_property("slot")
    except Exception:
        return None
    return s if isinstance(s, unreal.CanvasPanelSlot) else None


def _off(w):
    """Смещения слота (L, T, R, B) или None."""
    s = _slot(w)
    if s is None:
        return None
    m = s.get_editor_property("layout_data").get_editor_property("offsets")
    return (round(m.left, 3), round(m.top, 3), round(m.right, 3), round(m.bottom, 3))


def _r(v):
    """Округление числа для сверки (шум float не считается правкой)."""
    return round(float(v), 4)


def _brush_sig(w):
    """Кисть картинки для сверки: (текстура, UV min/max, тинт RGBA, draw_as, размер) или None у не-картинки."""
    if not isinstance(w, unreal.Image):
        return None
    # `_xy` — общий разбор вектора (в UE 5.8 часть свойств — `DeprecateSlateVector2D` без `.x`).
    from asda_ui_handmade import _xy
    br = w.get_editor_property("brush")
    res = br.get_editor_property("resource_object")
    tint = br.get_editor_property("tint_color").get_editor_property("specified_color")
    try:                                   # UV-регион читается не во всех версиях (как в asda_ui_handmade)
        uv = br.get_editor_property("uv_region")
        uvs = tuple(_r(c) for c in (_xy(uv.min) + _xy(uv.max)))
    except Exception:
        uvs = None
    sz = _xy(br.get_editor_property("image_size"))
    return (res.get_path_name() if res else None, uvs, (_r(tint.r), _r(tint.g), _r(tint.b), _r(tint.a)),
            str(br.get_editor_property("draw_as")), (_r(sz[0]), _r(sz[1])))


def _text_sig(w):
    """Текст и цвет TextBlock для сверки или None у не-текста."""
    if not isinstance(w, unreal.TextBlock):
        return None
    c = w.get_editor_property("color_and_opacity").get_editor_property("specified_color")
    return (str(w.get_editor_property("text")), (_r(c.r), _r(c.g), _r(c.b), _r(c.a)))


def _snapshot(root):
    """Снимок дерева: имя → (класс, родитель, смещения слота, видимость, непрозрачность, кисть, текст)."""
    snap = {}
    for w in _walk(root):
        par = w.get_parent()
        snap[w.get_name()] = (w.get_class().get_name(), par.get_name() if par else "-", _off(w),
                              str(w.get_editor_property("visibility")),
                              _r(w.get_editor_property("render_opacity")), _brush_sig(w), _text_sig(w))
    return snap


def _set_offsets(w, off):
    """Точная запись смещений слота (L, T, W, H), без авторазмера."""
    s = _slot(w)
    lay = s.get_editor_property("layout_data")
    lay.set_editor_property("offsets", unreal.Margin(off[0], off[1], off[2], off[3]))
    s.set_editor_property("layout_data", lay)
    s.set_editor_property("auto_size", False)


def _check_backup():
    """Бэкап ассета ДО записи — `asda_headless_tool.backup_file` (★ Т3, 11.10.2026): копии нет — снимается и сверяется по
    размеру; есть — исходник до первой правки, не трогается (несовпадение с текущим файлом — строка в журнал, не отказ).
    Отказ (исключение) — нет файла ассета, копия пустая или не прошла сверку. Возвращает путь копии."""
    bak = HT.backup_file(ASSET_FILE, BACKUP_SET, log=log)
    if bak is None:
        raise RuntimeError("нет файла ассета %s — бэкап невозможен, запись запрещена" % ASSET_FILE)
    return bak


def main(standalone=False):
    """Добавить недостающие узлы зон 78226/78265 и четыре плана полосы 78226 (см. шапку).
    `standalone` — ручной запуск: запись только при бэкапе этого захода. Из раннера пост-импорта (сразу после
    перегенерации окна) бэкап этого захода к новому ассету не относится — там проверяется только сверка узлов."""
    dry = bool(os.environ.get("ASDA_SMELT3_DRYRUN"))
    EAL = unreal.EditorAssetLibrary
    B = unreal.AsdaUmgBuilder
    if not EAL.does_asset_exist(ASSET):
        raise RuntimeError("нет ассета %s" % ASSET)
    data = plan()
    bp = EAL.load_asset(ASSET)
    root = B.get_root_canvas(bp)
    if not root:
        raise RuntimeError("нет корневого канваса у %s" % ASSET)
    atlas = unreal.load_asset(ATLAS)
    if not atlas:
        raise RuntimeError("нет атласа %s" % ATLAS)

    before = _snapshot(root)
    nodes = {w.get_name(): w for w in _walk(root)}
    added, differ = [], []
    for idn, (anchor, want_bar, want_visible) in ZONES.items():
        z = data[idn]
        a = nodes.get(anchor)
        grp = a.get_parent() if a else None
        if not isinstance(grp, unreal.CanvasPanel):
            raise RuntimeError("зона %d: не найдена группа рамки (якорь %s)" % (idn, anchor))
        # Начало рамки `Win_Tab` в координатах группы = левый-верх её плана 0 (`Win_Tab_<n>_0`):
        #   импортёр кладёт планы рамки со своим смещением, если группа шире (там вкладка-заголовок).
        tab0 = None
        for i in range(grp.get_children_count()):
            c = grp.get_child_at(i)
            nm = c.get_name()
            if nm.startswith("Win_Tab_") and nm.endswith("_0") and nm.count("_") == 3:
                tab0 = c
                break
        if tab0 is None:
            raise RuntimeError("зона %d: в группе %s нет плана 0 рамки Win_Tab" % (idn, grp.get_name()))
        t = _off(tab0)
        zx, zy = t[0] + z["origin"][0], t[1] + z["origin"][1]
        zw, zh = z["size"]
        want = [(z["idc"], "zone", (zx, zy, zw, zh), None)]
        if want_bar:
            for j in PLANE_ORDER:
                _, rx, ry, rw, rh, p = z["planes"][j]
                want.append((z["idc"] + PLANE_SUFFIX[j], "plane", (zx + rx, zy + ry, rw, rh), p))
        for name, kind, off, p in want:
            cur = nodes.get(name)
            if cur is not None:
                have = _off(cur)
                if have is None or any(abs(have[k] - off[k]) > EPS for k in range(4)):
                    differ.append("%s: в ассете %s, по данным %s — НЕ трогаю (возможна правка владельца)" % (name, have, off))
                if kind == "plane":
                    # ★ Волна 13 (М-4): и кисть — UV и тинт плана против данных скина.
                    sig = _brush_sig(cur)
                    d = p["diffuse"]
                    want_uv = tuple(_r(v) for v in p["uv"])
                    want_tint = tuple(_r(v) for v in d)
                    if sig is None:
                        differ.append("%s: в ассете не картинка — НЕ трогаю" % name)
                    else:
                        if sig[1] is not None and any(abs(a - b) > 1e-3 for a, b in zip(sig[1], want_uv)):
                            differ.append("%s: UV в ассете %s, по данным %s — НЕ трогаю (возможна правка владельца)" % (name, sig[1], want_uv))
                        if any(abs(a - b) > 1e-3 for a, b in zip(sig[2], want_tint)):
                            differ.append("%s: тинт в ассете %s, по данным %s — НЕ трогаю (возможна правка владельца)" % (name, sig[2], want_tint))
                continue
            added.append("%s %s (L%.0f T%.0f %.0f×%.0f) в %s" % (kind, name, off[0], off[1], off[2], off[3], grp.get_name()))
            if dry:
                continue
            if kind == "zone":
                w = B.add_hit_zone(bp, grp, name, unreal.Vector2D(off[0], off[1]), unreal.Vector2D(off[2], off[3]))
                if not w:
                    raise RuntimeError("не создан узел %s" % name)
                # `NoSelect = 1` — мышь насквозь; `Visible` — по решению владельца (78226) / по .JUI (78265).
                w.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE if want_visible
                                 else unreal.SlateVisibility.COLLAPSED)
            else:
                d = p["diffuse"]
                w = B.add_image(bp, grp, name, unreal.Vector2D(off[0], off[1]), unreal.Vector2D(off[2], off[3]),
                                atlas, unreal.Box2D(unreal.Vector2D(p["uv"][0], p["uv"][1]),
                                                    unreal.Vector2D(p["uv"][2], p["uv"][3])),
                                unreal.LinearColor(d[0], d[1], d[2], d[3]), False, unreal.Margin(0.0, 0.0, 0.0, 0.0))
                if not w:
                    raise RuntimeError("не создан узел %s" % name)
                # Мышь полосы ловит код опросом (`FAsdaScrollBarNomal::Tick`) — виджет клик не перехватывает.
                w.set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
            _set_offsets(w, off)
            nodes[name] = w

    for s in added:
        log("создать: %s" % s)
    for s in differ:
        log("осмотреть: %s" % s)
    if dry or not added:
        log("правок нет%s; создано 0, осмотреть %d" % (" (СУХОЙ ПРОГОН)" if dry else "", len(differ)))
        log("ПОЛОСА 78200: ГОТОВО")
        return

    # ★ Сверка «потеряно узлов: 0» ДО сохранения: все прежние узлы на месте и НЕ изменились.
    after = _snapshot(root)
    lost = [n for n in before if n not in after]
    changed = [n for n in before if n in after and after[n] != before[n]]
    new = [n for n in after if n not in before]
    log("узлов до %d, после %d, новых %d (%s); потеряно узлов: %d; изменено узлов: %d"
        % (len(before), len(after), len(new), ", ".join(sorted(new)), len(lost), len(changed)))
    if lost or changed:
        raise RuntimeError("сверка не прошла: потеряно %s, изменено %s — НЕ сохраняю" % (lost, changed))
    if standalone:
        _check_backup()
    # ⚠ `CompileAndSave` НЕ СОХРАНЯЕТ (имя врёт) — сохраняет `save_loaded_asset`.
    B.compile_and_save(bp)
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("не удалось сохранить %s" % ASSET)
    # Контроль чтением обратно.
    bp2 = EAL.load_asset(ASSET)
    names2 = {w.get_name() for w in _walk(B.get_root_canvas(bp2))}
    miss = [n for n in after if n not in names2]
    if miss:
        raise RuntimeError("после сохранения нет узлов: %s" % miss)
    log("сохранено: создано %d узлов, осмотреть %d" % (len(added), len(differ)))
    log("ПОЛОСА 78200: ГОТОВО")


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуля (раннер пост-импорта) — ничего.
if __name__ == "__main__":
    if "--plan" in sys.argv or unreal is None:
        print_plan()
    else:
        try:
            main(standalone=True)
        except Exception:
            unreal.log_error("[полоса 78200] ИСКЛЮЧЕНИЕ:\n%s" % traceback.format_exc())
