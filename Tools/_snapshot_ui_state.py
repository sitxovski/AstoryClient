# Copyright (c) 2026 @sitxovski (Discord)
# _snapshot_ui_state.py — МЕХАНИЧЕСКИЙ СНИМОК ВСЕГО UI-СЛОЯ (для сверки «до/после» переимпорта).
#
#   ЗАЧЕМ. `asda_jui_generic_umg.py` пересобирает окно С НУЛЯ, поэтому любой полный переимпорт
#   рискует стереть накопленные правки (регрессы 2026-08-06: мини-карта, ручные узлы чата).
#   Проверять «на глаз» такие потери нельзя — снимок снимается ДО и ПОСЛЕ прогона и сравнивается
#   скриптом `_diff_ui_state.py`. Любая ПРОПАЖА — дефект.
#
#   ЧТО СНИМАЕТСЯ по каждому WidgetBlueprint в /Game/AsdaAssets/UI (рекурсивно):
#     • строка `AsdaUmgBuilder.DumpWidgetTree` на КАЖДЫЙ виджет — имя, класс, родитель, видимость,
#       слот (анкор/оффсеты/выравнивание/zorder), цвет+текст у TextBlock, браш+UV+тинт у Image.
#       Именно UV ловит «лоскут атласа» вместо портрета NPC / контейнера 3D-сцены гачи.
#     • кнопки: `widget_style` — foreground'ы состояний подписи (Normal/Hovered/Pressed/Disabled),
#       NormalPadding/PressedPadding (смещение подписи), тинты кистей состояний, `is_focusable`
#       (кольцо фокуса UE) — ровно то, что зажигает переимпорт (713 кнопок).
#     • тексты: цвет, `auto_wrap_text` (закон `ui-autowrap-canvas-gotcha`: в Canvas обязан быть
#       false), выравнивание, кегль шрифта, сама строка.
#     • чекбоксы/радио: `DescribeCheckBoxStyle`.
#     • реестр `DA_JuiWindowMeta`: окна, ручки перетаскивания (полями), NoSelect-виджеты.
#
#   ЗАПУСК (headless, редактор должен быть ЗАКРЫТ):
#     set ASDA_SNAP_OUT=@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\ui_snapshot_before.json
#     UnrealEditor-Cmd.exe <проект> -ExecutePythonScript=<этот файл> ...
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
B = unreal.AsdaUmgBuilder

ROOT = "/Game/AsdaAssets/UI"
META_PATH = "/Game/AsdaAssets/UI/DA_JuiWindowMeta"
OUT = os.environ.get("ASDA_SNAP_OUT",
                     r"@@ASTORY_DEV_ROOT@@\AstoryClient\RawAssets\Data\ui_snapshot.json")


def _hex(c):
    """FLinearColor (ЛИНЕЙНЫЙ) -> sRGB-hex AARRGGBB — как в строках JuiSkin.ini."""
    def s(v):
        v = max(0.0, min(1.0, v))
        v = v * 12.92 if v <= 0.0031308 else 1.055 * (v ** (1.0 / 2.4)) - 0.055
        return int(round(v * 255.0))
    try:
        return "%02X%02X%02X%02X" % (int(round(max(0.0, min(1.0, c.a)) * 255.0)),
                                     s(c.r), s(c.g), s(c.b))
    except Exception:
        return "??"


def _fg(sc):
    """FSlateColor -> 'ЗАДАН:AARRGGBB' либо 'наследует' (UseForeground = состояний НЕТ)."""
    try:
        rule = str(sc.get_editor_property("color_use_rule"))
        spec = sc.get_editor_property("specified_color")
    except Exception:
        return "??"
    if "USE_COLOR_SPECIFIED" in rule:
        return "ЗАДАН:" + _hex(spec)
    return "наследует"


def _tint(br):
    try:
        return _hex(br.get_editor_property("tint_color").specified_color)
    except Exception:
        return "??"


def _pad(m):
    try:
        return "%.1f,%.1f,%.1f,%.1f" % (m.left, m.top, m.right, m.bottom)
    except Exception:
        return "??"


def _norm(line):
    """Схлопнуть выравнивающие пробелы дампа — иначе диф ловит форматирование, а не смысл."""
    return " ".join(line.split())


def _parse_dump(text):
    """Строки `DumpWidgetTree` -> {имя виджета: нормализованный остаток строки}."""
    out = {}
    for raw in text.split("\n"):
        line = raw.strip()
        if not line or line.startswith("==="):
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        out[parts[0]] = _norm(parts[1])
    return out


def _walk(w, fn):
    if w is None:
        return
    fn(w)
    try:
        kids = w.get_all_children()
    except Exception:
        return
    for c in kids:
        _walk(c, fn)


def _asset_record(bp):
    rec = {"tree": _parse_dump(B.dump_widget_tree(bp)),
           "buttons": {}, "texts": {}, "checks": {}}
    root = B.get_root_canvas(bp)

    def visit(w):
        name = w.get_name()
        if isinstance(w, unreal.Button):
            try:
                st = w.get_editor_property("widget_style")
                rec["buttons"][name] = {
                    "fgN": _fg(st.get_editor_property("normal_foreground")),
                    "fgH": _fg(st.get_editor_property("hovered_foreground")),
                    "fgP": _fg(st.get_editor_property("pressed_foreground")),
                    "fgD": _fg(st.get_editor_property("disabled_foreground")),
                    "padN": _pad(st.get_editor_property("normal_padding")),
                    "padP": _pad(st.get_editor_property("pressed_padding")),
                    "tintN": _tint(st.get_editor_property("normal")),
                    "tintH": _tint(st.get_editor_property("hovered")),
                    "tintP": _tint(st.get_editor_property("pressed")),
                    "focus": bool(w.get_editor_property("is_focusable")),
                }
            except Exception as e:                                # noqa: BLE001
                rec["buttons"][name] = {"ОШИБКА": str(e)}
        elif isinstance(w, unreal.TextBlock):
            try:
                f = w.get_editor_property("font")
                rec["texts"][name] = {
                    "цвет": _hex(w.get_editor_property("color_and_opacity").specified_color),
                    "wrap": bool(w.get_editor_property("auto_wrap_text")),
                    "wrapAt": round(float(w.get_editor_property("wrap_text_at")), 2),
                    "just": str(w.get_editor_property("justification")).split(".")[-1],
                    "кегль": int(f.get_editor_property("size")),
                    "текст": str(w.get_editor_property("text"))[:64],
                }
            except Exception as e:                                # noqa: BLE001
                rec["texts"][name] = {"ОШИБКА": str(e)}
        elif isinstance(w, unreal.CheckBox):
            try:
                rec["checks"][name] = _norm(B.describe_check_box_style(w))
            except Exception as e:                                # noqa: BLE001
                rec["checks"][name] = "ОШИБКА " + str(e)

    _walk(root, visit)
    return rec


def _meta_record():
    out = {}
    asset = EAL.load_asset(META_PATH)
    if asset is None:
        return out
    for m in asset.get_editor_property("windows"):
        handles = []
        for h in m.get_editor_property("drag_handles"):
            handles.append("%s->%s(id=%d,root=%d,fix=%d,tw=[%s])" % (
                h.get_editor_property("handle_widget"),
                h.get_editor_property("target_control"),
                int(h.get_editor_property("target_id_num")),
                1 if h.get_editor_property("target_is_window_root") else 0,
                1 if h.get_editor_property("original_bug_fixed") else 0,
                ",".join(sorted(str(x) for x in h.get_editor_property("target_widgets")))))
        out[str(m.get_editor_property("window_id"))] = {
            "idnum": int(m.get_editor_property("idnum")),
            "alwaysTop": bool(m.get_editor_property("always_top")),
            "ручки": sorted(handles),
            "noHit": sorted(str(x) for x in m.get_editor_property("no_hit_widgets")),
        }
    return out


def main():
    data = {"assets": {}, "meta": _meta_record()}
    paths = sorted(set(a.split(".")[0] for a in
                       EAL.list_assets(ROOT, recursive=True, include_folder=False)))
    n_w = n_b = n_t = n_cap = 0
    for p in paths:
        obj = EAL.load_asset(p)
        if obj is None or not isinstance(obj, unreal.WidgetBlueprint):
            continue
        try:
            rec = _asset_record(obj)
        except Exception as e:                                    # noqa: BLE001
            unreal.log_error("[снимок] %s: %s" % (p, e))
            continue
        data["assets"][p] = rec
        n_w += len(rec["tree"])
        n_b += len(rec["buttons"])
        n_t += len(rec["texts"])
        n_cap += sum(1 for b in rec["buttons"].values()
                     if str(b.get("fgN", "")).startswith("ЗАДАН")
                     and str(b.get("fgH", "")).startswith("ЗАДАН")
                     and str(b.get("fgP", "")).startswith("ЗАДАН"))
    handles = sum(len(v["ручки"]) for v in data["meta"].values())
    data["totals"] = {
        "окон(ассетов)": len(data["assets"]),
        "виджетов": n_w,
        "кнопок": n_b,
        "кнопок_с_состояниями_подписи": n_cap,
        "текстов": n_t,
        "окон_в_мете": len(data["meta"]),
        "ручек_перетаскивания": handles,
        "noHit_виджетов": sum(len(v["noHit"]) for v in data["meta"].values()),
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=0, sort_keys=True)
    unreal.log("=== СНИМОК UI: %s ===" % OUT)
    for k, v in data["totals"].items():
        unreal.log("    %-32s %d" % (k, v))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
