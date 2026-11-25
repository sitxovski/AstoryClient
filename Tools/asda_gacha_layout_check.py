# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# ★ ЧИСЛОВАЯ ПРОВЕРКА РАСКЛАДКИ СЦЕНЫ ПРЕМИУМ-ГАЧИ (headless, без игры).
#
# ЗАЧЕМ. Дефект «полосы автоматов и надписи стоят в левом верхнем углу» лечится тем, что позиции
# полос 68010+10i задаются ПРОЕКЦИЕЙ якоря (порт `deathRespawnUI_LayoutResurrectPanel@0xB6E480`),
# а не значениями из `.JUI` (там все дети окна 68000 лежат в (0,0)). Скрипт считает те же числа,
# что посчитает рантайм, и показывает, куда встанут группы полос при заданном размере кадра.
#
# ЧТО СЧИТАЕТСЯ (1:1 с `UAsdaPremiumGachaSubsystem::LayoutByProjection`):
#     цель  = (sx − w/2, sy − h/2 + yOff)          — эталонная формула для САМОГО контрола
#     груп. = цель − локальное смещение контрола внутри группы импортёра
# Локальное смещение и размер берутся из ДАМПА WBP (`Reports/headless_gacha_dump2.log`):
#     `Premium_PickBAR_{i+1}_grp` off(L−128 T0 R267 B148), внутри `Premium_PickBAR_{i+1}_{idx}_0`
#     off(L128 T0 R76 B62) ⇒ локальное смещение (128, 0), размер контрола 76×62.
#
# ЗАПУСК:  Tools\run_headless.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_gacha_layout_check.py
#   ASDA_GACHA_RT="2554x1354"  — размер кадра (по умолчанию как в живом логе владельца).
#   ASDA_GACHA_N=2             — сколько автоматов (у нашей premium_gatya_data.bin записей две).
import unreal, os

RT = os.environ.get("ASDA_GACHA_RT", "2554x1354")
N = int(os.environ.get("ASDA_GACHA_N", "2"))
INNER_LOCAL = (128.0, 0.0)   # смещение контрола-полосы внутри группы (дамп WBP)
INNER_SIZE = (76.0, 62.0)    # размер контрола 68010 (`gamemain_jui.ini`, Size = "76 62")


WBP = "/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Premium_PickBAR_Win"


def _read_wbp_slots():
    """Снять из РЕАЛЬНОГО WBP локальное смещение и размер контрола-полосы внутри группы —
    чтобы формула ниже считалась по данным ассета, а не по числам в коде."""
    bp = unreal.EditorAssetLibrary.load_asset(WBP)
    root = unreal.AsdaUmgBuilder.get_root_canvas(bp) if bp else None
    if not root:
        unreal.log_warning("[раскладка-гачи] WBP не открылся — беру числа из дампа")
        return None
    out = {}
    for i in range(root.get_children_count()):
        grp = root.get_child_at(i)
        if not grp or not grp.get_name().startswith("Premium_PickBAR_") or not grp.get_name().endswith("_grp"):
            continue
        if not isinstance(grp, unreal.PanelWidget):
            continue
        inner = None
        for c in range(grp.get_children_count()):
            ch = grp.get_child_at(c)
            if isinstance(ch, unreal.Image):
                inner = ch
                break
        if not inner:
            continue
        off = inner.slot.get_editor_property("layout_data").get_editor_property("offsets")
        out[grp.get_name()] = (off.left, off.top, off.right, off.bottom)
    return out


def main():
    w, h = (int(x) for x in RT.lower().split("x"))
    slots = _read_wbp_slots() or {}
    for k in sorted(slots):
        unreal.log("[раскладка-гачи] WBP %s: контрол внутри группы off(L%.0f T%.0f W%.0f H%.0f)"
                   % (k, slots[k][0], slots[k][1], slots[k][2], slots[k][3]))
    world = unreal.EditorLevelLibrary.get_editor_world()
    actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.AsdaGachaSceneActor, unreal.Vector(0, 0, 200000))
    if not actor:
        unreal.log_warning("[раскладка-гачи] актор сцены не заспавнился")
        return
    # Игрового вьюпорта в headless нет ⇒ актор возьмёт размер из RenderTargetFallback.
    actor.set_editor_property("render_target_fallback", unreal.IntPoint(w, h))
    actor.build_machines(N)
    actor.play_clip_all(1, True)

    size = actor.get_render_size()
    y_off = actor.get_slot_screen_y_offset()
    unreal.log("[раскладка-гачи] кадр %s · автоматов %d · yOffset %.0f"
               % (size, actor.get_machine_count(), y_off))

    for i in range(actor.get_machine_count()):
        rec = slots.get("Premium_PickBAR_%d_grp" % (i + 1))
        loc = (rec[0], rec[1]) if rec else INNER_LOCAL
        sz = (rec[2], rec[3]) if rec else INNER_SIZE
        p = actor.get_slot_screen_pos(i)          # UE-Python отдаёт только out-параметр
        ok = p.x > -1000.0                        # промах оригинал помечает −1125 (kOffScreen)
        tx = p.x - sz[0] * 0.5
        ty = p.y - sz[1] * 0.5 + y_off
        gx = tx - loc[0]
        gy = ty - loc[1]
        inside = (0 <= p.x <= w) and (0 <= p.y <= h)
        unreal.log("[раскладка-гачи] слот %d: проекция ок=%s (%.1f, %.1f) в кадре=%s → "
                   "контрол (%.1f, %.1f) → группа (%.1f, %.1f)"
                   % (i, ok, p.x, p.y, inside, tx, ty, gx, gy))
        b = actor.get_bone_screen_pos(i)
        okb = b.x > -1000.0
        unreal.log("[раскладка-гачи]   Bone19 ок=%s (%.1f, %.1f)" % (okb, b.x, b.y))

    unreal.EditorLevelLibrary.destroy_actor(actor)


# ★★★ ЗАКОН tools-no-import-side-effects: при импорте модуль ничего не делает.
if __name__ == "__main__":
    main()
