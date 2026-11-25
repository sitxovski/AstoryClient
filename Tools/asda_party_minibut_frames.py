# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# Крестик окна отряда `PARTY_MINIBUT` (UCheckBox, скин `CloseBtn_18_Party`).
#   ФАКТ из живого лога: импортёр разложил кадры со сдвигом —
#     Unchecked        (0.156,0.469)  <- ЧУЖОЙ кадр, тёмный: его и видит игрок в покое
#     UncheckedHovered (0.199,0.469)  <- чужой
#     UncheckedPressed (0.414,0.469)  <- А ЭТО и есть SS0 «обычный» из скина
#     Checked          (0.414,0.469)
#   Скин `CloseBtn_18_Party`: SS0 (0.414,0.469)-(0.449,0.504), SS2 (0.916,0.451)-(0.951,0.486),
#   SS1 в скине НЕТ. Раскладываем как в скине. ASDA_MINIBUT=revert — вернуть как было.
import os
import unreal

ASSET = "/Game/AsdaAssets/UI/Player_UI/Social/WBP_PARTY_WIN.WBP_PARTY_WIN"
SS0 = (0.414, 0.469, 0.449, 0.504)
SS2 = (0.916, 0.451, 0.951, 0.486)
WAS = {"unchecked_image": (0.156, 0.469, 0.191, 0.504),
       "unchecked_hovered_image": (0.199, 0.469, 0.234, 0.504),
       "unchecked_pressed_image": (0.414, 0.469, 0.449, 0.504)}
REVERT = os.environ.get("ASDA_MINIBUT", "").lower() == "revert"
UV_PROPS = ("uv_region", "UVRegion", "u_v_region")


def log(m):
    unreal.log("[крестик] " + str(m))


def collect(w, acc):
    if not w:
        return
    acc.append(w)
    try:
        n = w.get_children_count()
    except Exception:
        n = 0
    for i in range(n):
        collect(w.get_child_at(i), acc)


def set_uv(brush, box):
    """Ставит UV-регион кисти. Имя свойства в разных сборках разное — пробуем все."""
    region = unreal.Box2D(unreal.Vector2D(box[0], box[1]), unreal.Vector2D(box[2], box[3]))
    for name in UV_PROPS:
        try:
            brush.set_editor_property(name, region)
            return name
        except Exception:
            continue
    return None


def main():
    bp = unreal.load_object(None, ASSET)
    root = None
    try:
        root = unreal.AsdaUmgBuilder.get_root_canvas(bp)
    except Exception:
        pass
    if not root:
        wt = bp.get_editor_property("widget_tree")
        root = wt.get_editor_property("root_widget") if wt else None
    acc = []
    collect(root, acc)
    target = next((w for w in acc if w.get_name() == "PARTY_MINIBUT"), None)
    if not target:
        log("узел PARTY_MINIBUT не найден")
        return
    st = target.get_editor_property("widget_style")
    plan = {"unchecked_image": SS0, "unchecked_hovered_image": SS0, "unchecked_pressed_image": SS2}
    if REVERT:
        plan = WAS
    changed = 0
    for prop, box in plan.items():
        try:
            br = st.get_editor_property(prop)
        except Exception as e:
            log("%s: нет свойства (%s)" % (prop, e))
            continue
        used = set_uv(br, box)
        if not used:
            log("%s: НЕ УДАЛОСЬ задать UV — ни одно имя свойства не подошло %s" % (prop, UV_PROPS))
            continue
        st.set_editor_property(prop, br)
        log("%s -> UV (%.3f,%.3f)-(%.3f,%.3f) [свойство %s]" % ((prop,) + box + (used,)))
        changed += 1
    if changed:
        target.set_editor_property("widget_style", st)
        unreal.EditorAssetLibrary.save_loaded_asset(bp)   # CompileAndSave НЕ сохраняет
        log("сохранено, кистей изменено: %d (режим=%s)" % (changed, "ОТКАТ" if REVERT else "правка"))
    else:
        log("менять нечего")


if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
