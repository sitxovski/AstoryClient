# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ui_handmade.py — ★★★ РУЧНЫЕ ПРАВКИ ПОЛЬЗОВАТЕЛЯ ПЕРЕЖИВАЮТ ПЕРЕГЕНЕРАЦИЮ.
#
#   ★★★ ДВА КЛАССА РУЧНЫХ ПРАВОК — И ОБА ОБЯЗАНЫ БЫТЬ ПОД ЗАЩИТОЙ (урок 2026-08-06, второй
#   потери подряд). Первая версия этого файла умела только (A) и молча пропускала (B):
#
#     A. `WATCH` — узел ДОРИСОВАН В РЕДАКТОРЕ, в .JUI его нет вовсе (`RESIZE`, `SCROLL_UP`,
#        `SCROLL_DOWN` окна 2570). Перегенерация его СТИРАЕТ ⇒ надо ВОССОЗДАТЬ, если отсутствует.
#     B. `TWEAKS` — узел ЕСТЬ в .JUI, но пользователь ПОДВИНУЛ/ПЕРЕНАСТРОИЛ его в редакторе
#        (`CHAT_NEW_SERV_SYSbottn_3_0/_OPT0/_Text` окна 2570: заголовок-вкладка «Система» поднят
#        на T −284, т.е. на 24 px ВЫШЕ верхней кромки окна −260; .JUI даёт `Origin = "9 6"`
#        ⇒ машинная позиция L10 T−254). Перегенерация узел НЕ удаляет, а ПЕРЕЗАПИСЫВАЕТ его слот
#        значением из .JUI. Проверка «узел на месте?» такую потерю не видит В ПРИНЦИПЕ, и сверка
#        имён `asda_ui_name_diff.py` — тоже (имена не менялись). Отсюда:
#          • слот таких узлов ВОССТАНАВЛИВАЕТСЯ ПРИНУДИТЕЛЬНО (не «если отсутствует»);
#          • после восстановления идёт `verify()` со сверкой ЧИСЕЛ, и при расхождении прогон
#            ПАДАЕТ С ОШИБКОЙ (`RuntimeError`), а не пишет «готово» и едет дальше.
#
#   ПРОБЛЕМА. `asda_jui_generic_umg.py` пересобирает окно С НУЛЯ (`delete_asset` → `create_widget_bp`),
#   поэтому всё, чего нет в .JUI, исчезает молча. Так уже пропали узлы `RESIZE` / `SCROLL_UP` /
#   `SCROLL_DOWN` окна `WBP_CHAT_NEW_SYSTEM`, НАРИСОВАННЫЕ ЮЗЕРОМ В РЕДАКТОРЕ (растягивание и
#   прокрутка журнала чата — наше осознанное отступление, в оригинале их нет). Их восстановили из
#   бэкапа, после чего окно стало «нельзя перегенерировать» — мина замедленного действия.
#
#   ★ ПОЧЕМУ ИМЕННО СНИМОК+ВОССОЗДАНИЕ, А НЕ ОВЕРЛЕЙ. Рассмотрены три механизма:
#     1) отдельный WBP-оверлей поверх окна — НЕ годится: узлы должны жить В ДЕРЕВЕ окна
#        (`UAsdaChatSubsystem::FindNodeByName` ищет их в `WidgetTree` окна 2570, а хит-тест окна
#        считает прямоугольники по его же дереву); оверлей потребовал бы второй виджет в вьюпорте
#        со своим z и своей зоной — это новый UI, запрещено законом no-invented-ui;
#     2) «не перегенеривать это окно» — то, что есть сейчас: процессная мина, ломается при первом
#        же массовом прогоне;
#     3) СНИМОК свойств узла в JSON + воссоздание пост-импорт-шагом — узел остаётся в дереве окна,
#        механизм ОБЩИЙ (годится для любого будущего ручного узла в любом окне), и он проверяем:
#        после перегенерации сверка имён «до/после» обязана дать 0 расхождений.
#   Выбран (3). Ручная правка в редакторе НЕ запрещена — после неё надо снять новый снимок.
#
#   РАБОЧИЙ ЦИКЛ:
#     * нарисовал/поправил узлы в редакторе  →  снять снимок:
#         set ASDA_HANDMADE_SNAPSHOT=1 && Tools\run_headless.bat Tools\asda_ui_handmade.py
#       (список окон и имён — в `WATCH` ниже; снимок ложится в `Tools/data/ui_handmade_nodes.json`)
#     * перегенерация окон  →  шаг вызывается АВТОМАТИЧЕСКИ из `asda_ui_post_import.py`
#       и воссоздаёт недостающие узлы 1:1 по снимку.
#
#   ЧТО ИМЕННО ВОССТАНАВЛИВАЕТСЯ: класс узла, родитель, ИНДЕКС СРЕДИ БРАТЬЕВ (это z внутри канваса),
#   слот (анкоры, отступы, alignment, размер-по-контенту, z_order), видимость, клиппинг,
#   непрозрачность рендера и — для картинок — весь браш (текстура, UV-регион, тинт, draw_as,
#   tiling, margin, image_size).
#
#   Сухой прогон: `--dry-run` / ASDA_HANDMADE_DRYRUN=1 / ASDA_POSTSTEPS_DRYRUN=1 (`asda_ui_step_dryrun`,
#   07.10.2026): `restore()` работает как обычно, запись ассетов подавлена; итог — «[ШАГ-ДЕЛЬТА] …».
import json
import os
import re
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asda_ui_step_dryrun as SD   # общий сухой режим шагов пост-импорта (07.10.2026)

B = unreal.AsdaUmgBuilder
EAL = unreal.EditorAssetLibrary

# Собственный ключ сухого прогона шага (см. `asda_ui_step_dryrun.is_dry`).
DRYRUN_ENV = "ASDA_HANDMADE_DRYRUN"

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
SNAPSHOT = os.path.join(DATA, "ui_handmade_nodes.json")

# ★ РЕЕСТР (A) ДОРИСОВАННЫХ УЗЛОВ: путь окна → имена узлов, которых в .JUI НЕТ ВООБЩЕ.
#   Поведение: воссоздать, если после перегенерации узла нет.
#   Добавлять сюда КАЖДЫЙ раз, когда в сгенерированное окно дорисован узел вручную.
WATCH = {
    "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW_SYSTEM": ["RESIZE", "SCROLL_UP", "SCROLL_DOWN"],
}

# ★ РЕЕСТР (B) ПОДПРАВЛЕННЫХ УЗЛОВ: путь окна → имена узлов, которые .JUI СОЗДАЁТ, но пользователь
#   изменил им свойства в редакторе. Поведение: свойства из снимка НАВЯЗЫВАЮТСЯ ПОСЛЕ каждой
#   перегенерации (узел существует, поэтому «создать если нет» тут бесполезно).
#   ⚠ Что именно навязывать — поле `apply` в записи снимка (по умолчанию только `slot`):
#     "slot"       — анкоры/оффсеты/выравнивание/auto_size/z_order (ручной сдвиг, самый частый случай);
#     "visibility" — видимость;  "opacity" — render_opacity;  "clipping" — клиппинг;
#     "brush"      — кисть картинки;  "text" — текст и цвет TextBlock.
#   ★ Цвет ТЕКСТА намеренно НЕ в списке по умолчанию: цвета правит закон `ui-srgb-gamma-rule`
#     (импортёр кладёт sRGB→linear), и навязывать поверх него старое значение нельзя.
TWEAKS = {
    "/Game/AsdaAssets/UI/Player_UI/Chat/WBP_CHAT_NEW_SYSTEM": [
        "CHAT_NEW_SERV_SYSbottn_3_0",
        "CHAT_NEW_SERV_SYSbottn_OPT0",
        "CHAT_NEW_SERV_SYSbottn_Text",
    ],
}

# Допуск сверки чисел слота (px базы 1024×768).
EPS = 0.02


# ── общее ─────────────────────────────────────────────────────────────────────────────────────
def V2(x, y):
    return unreal.Vector2D(float(x), float(y))


def _xy(v):
    """★ В UE 5.8 часть свойств отдаёт `DeprecateSlateVector2D` — у него НЕТ атрибута `.x`."""
    for get in (lambda: (v.x, v.y), lambda: (v.X, v.Y), lambda: (v[0], v[1])):
        try:
            a, b = get()
            return [float(a), float(b)]
        except Exception:
            continue
    s = str(v).replace("(", " ").replace(")", " ").replace(",", " ").replace("=", " ")
    nums = [t for t in s.split() if _isnum(t)]
    return [float(nums[0]), float(nums[1])] if len(nums) >= 2 else [0.0, 0.0]


def _isnum(t):
    try:
        float(t)
        return True
    except ValueError:
        return False


def _children(w):
    try:
        return list(w.get_all_children())
    except Exception:
        return []


def _walk(root):
    """(узел, родитель) по всему дереву виджетов."""
    stack = [(root, None)]
    while stack:
        w, p = stack.pop()
        yield w, p
        for c in _children(w):
            stack.append((c, w))


def _find(bp, name):
    for w, _p in _walk(B.get_root_canvas(bp)):
        if w.get_name() == name:
            return w
    return None


def _parent_of(bp, node):
    for w, p in _walk(B.get_root_canvas(bp)):
        if w == node:
            return p
    return None


# ── снятие снимка ─────────────────────────────────────────────────────────────────────────────
def _dump_slot(w):
    slot = w.slot
    if not isinstance(slot, unreal.CanvasPanelSlot):
        return None
    lay = slot.get_editor_property("layout_data")
    off = lay.offsets
    anc = lay.anchors
    return {
        "anchors": _xy(anc.minimum) + _xy(anc.maximum),
        "offsets": [float(off.left), float(off.top), float(off.right), float(off.bottom)],
        "alignment": _xy(lay.alignment),
        "auto_size": bool(slot.get_editor_property("auto_size")),
        "z_order": int(slot.get_editor_property("z_order")),
    }


def _dump_brush(w):
    if not isinstance(w, unreal.Image):
        return None
    br = w.get_editor_property("brush")
    res = br.get_editor_property("resource_object")
    tint = br.get_editor_property("tint_color").get_editor_property("specified_color")
    mar = br.get_editor_property("margin")
    out = {
        "texture": res.get_path_name().split(".")[0] if res else None,
        "image_size": _xy(br.get_editor_property("image_size")),
        "tint": [tint.r, tint.g, tint.b, tint.a],
        "draw_as": str(br.get_editor_property("draw_as")),
        "tiling": str(br.get_editor_property("tiling")),
        "margin": [mar.left, mar.top, mar.right, mar.bottom],
    }
    try:                                   # UV-регион читается не во всех версиях
        uv = br.get_editor_property("uv_region")
        out["uv"] = _xy(uv.min) + _xy(uv.max)
    except Exception:
        out["uv"] = [0.0, 0.0, 0.0, 0.0]
    return out


def _dump_text(w):
    """Текстовое ОФОРМЛЕНИЕ узла.

    ★ РАСШИРЕНО 2026-08-27 (пункт С7 аудита `audit_runtime_vs_assets_SUMMARY.md`).
      ЧТО БЫЛО: снимались только `text` и `color`. Из-за этого КЕГЛЬ, НАЧЕРТАНИЕ и ОБВОДКА
      ручных правок перегенерацию НЕ переживали — а именно обводка сейчас главный источник
      дефектов (импортёр ставит её 7059 контролам из 7862, читая НЕ ТО поле: 925 = флаги
      переноса, обводку даёт 926). Пока снимок их не покрывал, любой перенос оформления
      в ассеты был миной: первая же перегенерация вернула бы импортёрскую обводку.
    """
    if not isinstance(w, unreal.TextBlock):
        return None
    col = w.get_editor_property("color_and_opacity").get_editor_property("specified_color")
    rec = {"text": str(w.get_editor_property("text")),
           "color": [col.r, col.g, col.b, col.a]}
    try:
        f = w.get_editor_property("font")
        oc = f.get_editor_property("outline_settings").get_editor_property("outline_color")
        rec["font"] = {
            "size": int(f.get_editor_property("size")),
            "typeface": str(f.get_editor_property("typeface_font_name")),
            "outline_size": int(f.get_editor_property("outline_settings")
                                 .get_editor_property("outline_size")),
            "outline_color": [oc.r, oc.g, oc.b, oc.a],
        }
    except Exception as e:                                   # noqa: BLE001
        unreal.log_warning("[ручные-узлы] шрифт узла %s не снят: %s" % (w.get_name(), e))
        rec["font"] = None
    return rec


def _dump_node(bp, root, path, name, kind):
    """Снять запись одного узла. kind: "node" (дорисованный) | "tweak" (подправленный)."""
    node = _find(bp, name)
    if not node:
        unreal.log_error("[ручные-узлы] СНИМОК: в %s нет узла %s — снимать нечего "
                         "(восстанови ассет из @@ASTORY_DEV_ROOT_ESC@@\\_backup\\UI_*)" % (path, name))
        return None
    parent = _parent_of(bp, node)
    siblings = _children(parent) if parent else []
    rec = {
        "name": name,
        "class": node.get_class().get_name(),
        "parent": (parent.get_name() if parent and parent != root else "ROOT"),
        "index": siblings.index(node) if node in siblings else -1,
        "visibility": str(node.get_editor_property("visibility")),
        "render_opacity": float(node.get_editor_property("render_opacity")),
        "slot": _dump_slot(node),
        "brush": _dump_brush(node),
        "text": _dump_text(node),
    }
    try:
        rec["clipping"] = str(node.get_editor_property("clipping"))
    except Exception:
        rec["clipping"] = None
    if kind == "tweak":
        # ★ У подправленного узла навязываем ТОЛЬКО то, что перечислено. По умолчанию — слот:
        #   именно его перезаписывает импортёр значением Origin/Size из .JUI.
        rec["apply"] = ["slot"]
    return rec


def snapshot():
    data = {"version": 2, "nodes": {}, "tweaks": {}}
    for section, registry, kind in (("nodes", WATCH, "node"), ("tweaks", TWEAKS, "tweak")):
        for path, names in registry.items():
            if not EAL.does_asset_exist(path):
                unreal.log_warning("[ручные-узлы] нет ассета %s" % path)
                continue
            bp = EAL.load_asset(path)
            root = B.get_root_canvas(bp)
            recs = []
            for name in names:
                rec = _dump_node(bp, root, path, name, kind)
                if not rec:
                    continue
                recs.append(rec)
                unreal.log("[ручные-узлы] снят [%s] %s / %s (%s)"
                           % (kind, path.rsplit("/", 1)[-1], name, rec["class"]))
            if recs:
                data[section][path] = recs
    if not os.path.isdir(DATA):
        os.makedirs(DATA)
    with open(SNAPSHOT, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=1)
    unreal.log("[ручные-узлы] снимок → %s (окон с узлами %d, с правками %d)"
               % (SNAPSHOT, len(data["nodes"]), len(data["tweaks"])))
    return data


def load_snapshot():
    """Прочитать снимок. Понимает и СТАРЫЙ плоский формат v1 ({путь: [записи]})."""
    if not os.path.isfile(SNAPSHOT):
        return {"version": 2, "nodes": {}, "tweaks": {}}
    with open(SNAPSHOT, encoding="utf-8") as f:
        raw = json.load(f)
    if isinstance(raw, dict) and "nodes" in raw:
        raw.setdefault("tweaks", {})
        return raw
    return {"version": 1, "nodes": raw, "tweaks": {}}          # v1: всё было «дорисованными»


# ── восстановление ────────────────────────────────────────────────────────────────────────────
def _enum(enum_type, s, default):
    """Строка перечисления → значение.

    ★ UE-Python печатает перечисление как `<SlateVisibility.SELF_HIT_TEST_INVISIBLE: 4>`,
    поэтому имя достаётся регуляркой, а не отрезанием по точке (иначе останется «…: 4>»).
    """
    if not s:
        return default
    m = re.findall(r"[A-Za-z_][A-Za-z_0-9]*", s)
    for key in reversed(m):
        if hasattr(enum_type, key):
            return getattr(enum_type, key)
    return default


def _apply_slot(node, s):
    if not s:
        return
    slot = node.slot
    if not isinstance(slot, unreal.CanvasPanelSlot):
        return
    lay = slot.get_editor_property("layout_data")
    lay.anchors = unreal.Anchors(V2(s["anchors"][0], s["anchors"][1]), V2(s["anchors"][2], s["anchors"][3]))
    lay.offsets = unreal.Margin(s["offsets"][0], s["offsets"][1], s["offsets"][2], s["offsets"][3])
    lay.alignment = V2(s["alignment"][0], s["alignment"][1])
    slot.set_editor_property("layout_data", lay)
    slot.set_editor_property("auto_size", bool(s["auto_size"]))
    slot.set_editor_property("z_order", int(s["z_order"]))


def _set(obj, prop, value, what):
    """Присвоить свойство, не роняя весь шаг из-за одного несовместимого поля.

    ★ Грабля UE 5.8: `SlateBrush.ImageSize` объявлен как `DeprecateSlateVector2D`, и присвоение
    обычного `Vector2D` бросает «Failed to convert type 'Vector2D' to property 'ImageSize'».
    Из-за этого первый же прогон восстановления упал на `RESIZE` и не дошёл до `SCROLL_UP/DOWN`.
    """
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception as e:                                  # noqa: BLE001
        unreal.log_warning("[ручные-узлы] %s: свойство %s не присвоено (%s)" % (what, prop, e))
        return False


def _apply_brush(node, b):
    if not b or not isinstance(node, unreal.Image):
        return
    br = node.get_editor_property("brush")
    if b.get("texture"):
        tex = EAL.load_asset(b["texture"])
        if tex:
            br.set_editor_property("resource_object", tex)
        else:
            unreal.log_warning("[ручные-узлы] текстура не найдена: %s" % b["texture"])
    # ImageSize=(0,0) означает «размер берётся из слота» — присваивать нечего.
    if tuple(b["image_size"]) != (0.0, 0.0):
        _set(br, "image_size", V2(*b["image_size"]), node.get_name())
    sc = unreal.SlateColor()
    sc.set_editor_property("specified_color", unreal.LinearColor(*b["tint"]))
    sc.set_editor_property("color_use_rule", unreal.SlateColorStylingMode.USE_COLOR_SPECIFIED)
    nm = node.get_name()
    _set(br, "tint_color", sc, nm)
    _set(br, "draw_as", _enum(unreal.SlateBrushDrawType, b.get("draw_as"),
                              unreal.SlateBrushDrawType.IMAGE), nm)
    _set(br, "tiling", _enum(unreal.SlateBrushTileType, b.get("tiling"),
                             unreal.SlateBrushTileType.NO_TILE), nm)
    _set(br, "margin", unreal.Margin(*b["margin"]), nm)
    uv = b.get("uv") or [0, 0, 0, 0]
    if tuple(uv) != (0.0, 0.0, 0.0, 0.0):
        _set(br, "uv_region", unreal.Box2D(V2(uv[0], uv[1]), V2(uv[2], uv[3])), nm)
    node.set_editor_property("brush", br)


def _create(bp, parent, rec):
    """Создать узел нужного класса. Позиция/размер потом перезаписываются точным слотом."""
    cls = rec["class"]
    name = rec["name"]
    pos, size = V2(0, 0), V2(10, 10)
    if cls == "Image":
        return B.add_image(bp, parent, name, pos, size, None,
                           unreal.Box2D(V2(0, 0), V2(0, 0)), unreal.LinearColor(1, 1, 1, 1),
                           False, unreal.Margin(0, 0, 0, 0))
    if cls == "CanvasPanel":
        return B.add_canvas(bp, parent, name, pos, size)
    if cls == "Button":
        return B.add_button(bp, parent, name, pos, size)
    if cls == "TextBlock":
        t = rec.get("text") or {}
        return B.add_text(bp, parent, name, pos, size, t.get("text", ""), 8,
                          unreal.LinearColor(*(t.get("color") or [1, 1, 1, 1])), 0, 0)
    unreal.log_error("[ручные-узлы] класс %s не поддержан (узел %s)" % (cls, name))
    return None


def _apply_text(node, t):
    if not t or not isinstance(node, unreal.TextBlock):
        return
    # ★ UE-Python принимает и `unreal.Text`, и обычную строку — в разных версиях по-разному,
    #   поэтому пробуем оба, а не полагаемся на одну форму.
    s = t.get("text", "")
    try:
        node.set_editor_property("text", unreal.Text(s))
    except Exception:                                       # noqa: BLE001
        _set(node, "text", s, node.get_name())
    col = t.get("color")
    if col:
        sc = unreal.SlateColor()
        sc.set_editor_property("specified_color", unreal.LinearColor(*col))
        sc.set_editor_property("color_use_rule", unreal.SlateColorStylingMode.USE_COLOR_SPECIFIED)
        _set(node, "color_and_opacity", sc, node.get_name())
    # ★ С7: КЕГЛЬ, НАЧЕРТАНИЕ и ОБВОДКА — без них ручная правка оформления не переживала
    #   перегенерацию (см. `_dump_text`).
    f = t.get("font")
    if f:
        try:
            fi = node.get_editor_property("font")
            fi.set_editor_property("size", int(f["size"]))
            fi.set_editor_property("typeface_font_name", unreal.Name(f["typeface"]))
            os_ = fi.get_editor_property("outline_settings")
            os_.set_editor_property("outline_size", int(f["outline_size"]))
            oc = f.get("outline_color")
            if oc:
                os_.set_editor_property("outline_color", unreal.LinearColor(*oc))
            fi.set_editor_property("outline_settings", os_)
            _set(node, "font", fi, node.get_name())
        except Exception as e:                               # noqa: BLE001
            unreal.log_error("[ручные-узлы] шрифт узла %s НЕ восстановлен: %s" % (node.get_name(), e))


def _apply_aspects(node, rec, aspects):
    """Навязать узлу перечисленные аспекты снимка."""
    nm = rec["name"]
    if "slot" in aspects:
        _apply_slot(node, rec["slot"])
    if "brush" in aspects:
        _apply_brush(node, rec["brush"])
    if "text" in aspects:
        _apply_text(node, rec.get("text"))
    if "visibility" in aspects:
        _set(node, "visibility",
             _enum(unreal.SlateVisibility, rec.get("visibility"), unreal.SlateVisibility.VISIBLE), nm)
    if "opacity" in aspects:
        _set(node, "render_opacity", float(rec.get("render_opacity", 1.0)), nm)
    if "clipping" in aspects and rec.get("clipping"):
        _set(node, "clipping", _enum(unreal.WidgetClipping, rec["clipping"],
                                     unreal.WidgetClipping.INHERIT), nm)


def restore(dry=False, delta=None):
    """Вернуть окнам ВСЕ ручные правки: (A) воссоздать удалённые узлы, (B) навязать слоты
    подправленным. Возвращает (создано, поправлено, окон не найдено).
    `dry` — запись ассетов подавлена; `delta` — `asda_ui_step_dryrun.WidgetDelta` (None — без учёта)."""
    data = load_snapshot()
    if not data["nodes"] and not data["tweaks"]:
        unreal.log_warning("[ручные-узлы] снимка нет (%s) — восстанавливать нечего" % SNAPSHOT)
        return 0, 0, 0
    made = 0
    fixed = 0
    absent = 0
    for path in sorted(set(data["nodes"]) | set(data["tweaks"])):
        if not EAL.does_asset_exist(path):
            unreal.log_warning("[ручные-узлы] нет ассета %s" % path)
            absent += 1
            continue
        bp = EAL.load_asset(path)
        if delta is not None:
            delta.take(bp)
        root = B.get_root_canvas(bp)
        changed = False

        # ── (A) ДОРИСОВАННЫЕ УЗЛЫ: создать, если после перегенерации их нет ────────────────────
        for rec in data["nodes"].get(path, []):
            if _find(bp, rec["name"]):
                continue                                    # узел на месте — не трогаем
            # ★ Каждый узел — в своей защите: сбой на одном не должен лишать окно остальных
            #   (первый прогон упал на `RESIZE` и не дошёл до `SCROLL_UP`/`SCROLL_DOWN`).
            try:
                parent = root if rec["parent"] == "ROOT" else (_find(bp, rec["parent"]) or root)
                node = _create(bp, parent, rec)
                if not node:
                    continue
                # ★ С7 (2026-08-27): добавлен "text" — у ВОССОЗДАВАЕМОГО узла оформление
                #   (кегль, начертание, обводка, цвет) раньше не восстанавливалось вовсе,
                #   и узел возвращался с дефолтным шрифтом. Для узлов без текста это no-op.
                _apply_aspects(node, rec, ("slot", "brush", "text", "visibility", "opacity", "clipping"))
                idx = int(rec.get("index", -1))
                if idx >= 0:
                    B.move_child_to_index(parent, node, min(idx, parent.get_children_count() - 1))
            except Exception as e:                          # noqa: BLE001
                unreal.log_error("[ручные-узлы] узел %s НЕ восстановлен: %s" % (rec["name"], e))
                continue
            made += 1
            changed = True
            unreal.log("[ручные-узлы] ВОССОЗДАН %s / %s (%s)"
                       % (path.rsplit("/", 1)[-1], rec["name"], rec["class"]))

        # ── (B) ПОДПРАВЛЕННЫЕ УЗЛЫ: слот навязываем ВСЕГДА, узел при этом существует ───────────
        for rec in data["tweaks"].get(path, []):
            node = _find(bp, rec["name"])
            if not node:
                unreal.log_error("[ручные-узлы] ПРАВКА: в %s нет узла %s — перегенерация его не "
                                 "создала, снимок применить некуда" % (path, rec["name"]))
                continue
            aspects = tuple(rec.get("apply") or ["slot"])
            before = _dump_slot(node)
            try:
                _apply_aspects(node, rec, aspects)
            except Exception as e:                          # noqa: BLE001
                unreal.log_error("[ручные-узлы] правка %s НЕ применена: %s" % (rec["name"], e))
                continue
            after = _dump_slot(node)
            if before != after:
                fixed += 1
                changed = True
                unreal.log("[ручные-узлы] ВЕРНУТА РУЧНАЯ ПРАВКА %s / %s: слот %s → %s"
                           % (path.rsplit("/", 1)[-1], rec["name"],
                              (before or {}).get("offsets"), (after or {}).get("offsets")))
        if changed:
            B.compile_and_save(bp)
            SD.save(bp, dry, "asda_ui_handmade")
        if delta is not None:
            delta.done(bp)
    unreal.log("[ручные-узлы] восстановление: создано %d узлов, возвращено правок %d, окон не найдено %d%s"
               % (made, fixed, absent, " (СУХОЙ)" if dry else ""))
    return made, fixed, absent


# ── строгая проверка ──────────────────────────────────────────────────────────────────────────
def _slot_diff(a, b):
    """Список расхождений двух слотов (или None, если совпадают)."""
    if a is None or b is None:
        return None if a == b else ["слот отсутствует"]
    bad = []
    for key in ("anchors", "offsets", "alignment"):
        for i, (x, y) in enumerate(zip(a[key], b[key])):
            if abs(float(x) - float(y)) > EPS:
                bad.append("%s[%d] %.3f≠%.3f" % (key, i, x, y))
    for key in ("auto_size", "z_order"):
        if a[key] != b[key]:
            bad.append("%s %s≠%s" % (key, a[key], b[key]))
    return bad or None


def _text_diff(want, got):
    """Расхождения ОФОРМЛЕНИЯ текста (кегль, начертание, обводка, цвет).

    ★ С7 (2026-08-27): без этой сверки расширенный снимок бесполезен — восстановление шрифта
      прошло бы «молча мимо», как это было со слотом до 2026-08-06.
    """
    bad = []
    if not want or not got:
        return bad
    wf, gf = want.get("font"), got.get("font")
    if wf and gf:
        if int(wf["size"]) != int(gf["size"]):
            bad.append("кегль %s→%s" % (wf["size"], gf["size"]))
        if str(wf["typeface"]) != str(gf["typeface"]):
            bad.append("начертание %s→%s" % (wf["typeface"], gf["typeface"]))
        if int(wf["outline_size"]) != int(gf["outline_size"]):
            bad.append("обводка %s→%s" % (wf["outline_size"], gf["outline_size"]))
    wc, gc = want.get("color"), got.get("color")
    if wc and gc and any(abs(a - b) > 1e-3 for a, b in zip(wc, gc)):
        bad.append("цвет %s→%s" % ([round(x, 3) for x in wc], [round(x, 3) for x in gc]))
    return bad


def verify():
    """СТРОГАЯ сверка ассетов со снимком. Возвращает список проблем (пустой = всё хорошо).

    ★ Ради этой функции всё и затевалось: сверка ИМЁН (`asda_ui_name_diff.py`) слепа к сдвигу
    узла, а структурный слепок надо ещё прочитать глазами — и ровно на этом 2026-08-06 потеряли
    заголовок «Система». Здесь сверяются ЧИСЛА, и результат — не совет, а причина падения прогона.
    """
    data = load_snapshot()
    problems = []
    for path in sorted(set(data["nodes"]) | set(data["tweaks"])):
        if not EAL.does_asset_exist(path):
            problems.append("%s: АССЕТА НЕТ" % path)
            continue
        bp = EAL.load_asset(path)
        short = path.rsplit("/", 1)[-1]
        for rec in data["nodes"].get(path, []):
            node = _find(bp, rec["name"])
            if not node:
                problems.append("%s / %s: ДОРИСОВАННЫЙ УЗЕЛ ПОТЕРЯН" % (short, rec["name"]))
                continue
            bad = _slot_diff(rec["slot"], _dump_slot(node))
            if bad:
                problems.append("%s / %s: слот разошёлся со снимком (%s)"
                                % (short, rec["name"], "; ".join(bad)))
            # ★ С7: у дорисованного узла оформление — часть правки, сверяем всегда.
            bad = _text_diff(rec.get("text"), _dump_text(node))
            if bad:
                problems.append("%s / %s: оформление разошлось со снимком (%s)"
                                % (short, rec["name"], "; ".join(bad)))
        for rec in data["tweaks"].get(path, []):
            node = _find(bp, rec["name"])
            if not node:
                problems.append("%s / %s: УЗЕЛ С РУЧНОЙ ПРАВКОЙ ОТСУТСТВУЕТ" % (short, rec["name"]))
                continue
            if "slot" in (rec.get("apply") or ["slot"]):
                bad = _slot_diff(rec["slot"], _dump_slot(node))
                if bad:
                    problems.append("%s / %s: РУЧНАЯ ПРАВКА НЕ ПРИМЕНИЛАСЬ (%s)"
                                    % (short, rec["name"], "; ".join(bad)))
            # ★ С7: оформление сверяем, когда оно навязывается (`apply` содержит "text").
            if "text" in (rec.get("apply") or []):
                bad = _text_diff(rec.get("text"), _dump_text(node))
                if bad:
                    problems.append("%s / %s: ОФОРМЛЕНИЕ НЕ ПРИМЕНИЛОСЬ (%s)"
                                    % (short, rec["name"], "; ".join(bad)))
    if problems:
        for p in problems:
            unreal.log_error("[ручные-узлы] %s" % p)
    else:
        n = sum(len(v) for v in data["nodes"].values()) + sum(len(v) for v in data["tweaks"].values())
        unreal.log("[ручные-узлы] ПРОВЕРКА: все %d ручных правок на месте" % n)
    return problems


def step_visibility_nodes():
    """★ 07.10.2026: [(путь окна, узел, причина)] — ручные узлы владельца, СВЁРНУТЫЕ в снимке
    (`Tools/data/ui_handmade_nodes.json`, разделы `nodes` и `tweaks`). Синхронизатор видимости
    (`asda_ui_sync_own_visibility`) их не раскрывает: это правка владельца, а не след импортёра."""
    data = load_snapshot()
    out = []
    for section in ("nodes", "tweaks"):
        for path, recs in data.get(section, {}).items():
            for rec in recs:
                v = str(rec.get("visibility", "")).upper()
                if "COLLAPSED" in v or "HIDDEN" in v:
                    out.append((path, rec["name"], "ручная правка владельца (снимок %s, вид %s)"
                                % (section, rec.get("visibility"))))
    return out


def main():
    if os.environ.get("ASDA_HANDMADE_SNAPSHOT"):
        snapshot()
        return
    dry = SD.is_dry(DRYRUN_ENV)                                  # сухой прогон (запись подавлена)
    delta = SD.WidgetDelta("asda_ui_handmade", dry)              # учёт правок шага по снимкам дерева
    restore(dry, delta)
    delta.finish()
    problems = verify()
    if problems:
        # ★★★ ПАДАЕМ ГРОМКО. Молчаливое «продолжаем» уже дважды стоило пользователю его правок.
        raise RuntimeError("ручные правки пользователя НЕ восстановлены (%d): %s"
                           % (len(problems), " | ".join(problems)))


# ★★★ ЗАКОН tools-no-import-side-effects.
if __name__ == "__main__":
    main()
