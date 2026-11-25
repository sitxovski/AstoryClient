# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_ministate_name_live_check.py — ЖИВАЯ (PIE) проверка ВЕРТИКАЛЬНОГО ВЫРАВНИВАНИЯ подписей
#   окна мини-статуса (имя персонажа, числа HP/MP, вес).
#
# ★★★ ЗАЧЕМ ЖИВАЯ (закон `false-negatives-method`): офлайн-дамп .uasset показывает Top слота,
#   но НЕ показывает, где реально ляжет строка — это решает измеренная высота строки шрифта
#   (`FSlateFontMeasure::GetMaxCharacterHeight`), которой в офлайне нет. Снимаем кадр и
#   печатаем ЖИВУЮ геометрию каждой подписи.
#
# ЗАПУСК: Tools\run_headless_ministate_name.bat
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/ministate_name_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"
WBP = "/Game/AsdaAssets/UI/Player_UI/HUD/WBP_MINISTATE_WIN"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 60
MAX_TICKS = 30000

# подпись -> (текст для показа, высота контрола в .JUI, маска EXS TextAlign, EXS TextPos.y)
LABELS = [
    ("MINISTATE_CharName",  "GM_sitxovski", 17.0, 5, 1.0),
    ("MINISTATE_HPTEXT",    "2899/2899",    10.0, 5, 0.0),
    ("MINISTATE_MPTEXT",    "162/162",      10.0, 5, 0.0),
    ("MINISTATE_WEIGHTTEXT", "37",          12.0, 4, 0.0),
]


def log(m):
    import unreal
    unreal.log("[мини-имя-живой] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    def win():
        # ★ `unreal.SubsystemBlueprintLibrary` в обёртке отсутствует (проверено запуском), поэтому
        #   живой экземпляр окна берём перебором виджетов вьюпорта по имени класса WBP.
        w = world()
        if not w:
            return None
        try:
            all_w = unreal.WidgetBlueprintLibrary.get_all_widgets_of_class(w, unreal.UserWidget, False)
        except Exception as e:
            log("!! get_all_widgets_of_class: %s" % e)
            return None
        for uw in all_w:
            if "MINISTATE_WIN" in uw.get_class().get_name():
                return uw
        log("!! окно MINISTATE_WIN среди %d виджетов не найдено: %s"
            % (len(all_w), [x.get_class().get_name() for x in all_w][:20]))
        return None

    def s_travel():
        cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode")

    def s_open():
        cmd("asda.UI.OpenByPath MINISTATE_WIN %s 10" % WBP)

    def s_settext():
        # ★ Подписи ставим КОНСОЛЬЮ (`asda.UI.SetText`): дотянуться до живого UUserWidget из Python
        #   нечем — ни `SubsystemBlueprintLibrary`, ни `WidgetBlueprintLibrary` в обёртку не попали
        #   (проверено запуском). Команда общая, лежит в `AsdaWindowSubsystem`.
        for name, txt, jui_h, mask, tpy in LABELS:
            cmd("asda.UI.SetText MINISTATE_WIN %s %s" % (name, txt))

    def s_metrics():
        cmd("asda.UI.DumpTextMetrics MINISTATE_WIN")

    def s_align():
        # ★ ТОТ ЖЕ общий код, что зовёт `UAsdaMiniStateSubsystem::SetupControls` (маски и высоты — из .JUI).
        for name, txt, jui_h, mask, tpy in LABELS[:3]:
            cmd("asda.UI.AlignText MINISTATE_WIN %s %d %.0f %.0f" % (name, mask, jui_h, tpy))

    def _slot(w):
        try:
            s = w.slot
            if isinstance(s, unreal.CanvasPanelSlot):
                return s
        except Exception:
            pass
        return None

    def s_dump(tag):
        uw = win()
        if not uw:
            log("!! окна нет")
            return
        log("── ЖИВАЯ ГЕОМЕТРИЯ ПОДПИСЕЙ (%s) ──" % tag)
        for name, txt, jui_h, mask, tpy in LABELS:
            tb = uw.get_widget_from_name(name)
            if not tb:
                continue
            s = _slot(tb)
            o = s.get_offsets() if s else None
            try:
                ds = tb.get_desired_size()
            except Exception:
                ds = None
            try:
                g = tb.get_cached_geometry()
                gsz = unreal.SlateBlueprintLibrary.get_local_size(g)
                gabs = unreal.SlateBlueprintLibrary.local_to_absolute(g, unreal.Vector2D(0, 0))
            except Exception as e:
                gsz = gabs = "?%s" % e
            log("%-22s JUI_h=%-5.1f маска=%d слот(T=%.2f H=%.2f) desired=%s геом(размер=%s абс=%s)"
                % (name, jui_h, mask, o.top if o else -1, o.bottom if o else -1, ds, gsz, gabs))
        # родительские плашки — чтобы знать, В ЧЁМ центрировать
        for pn in ("MINISTATE_CharName_101_0", "MINISTATE_CharName_grp"):
            pw = uw.get_widget_from_name(pn)
            if not pw:
                continue
            s = _slot(pw)
            o = s.get_offsets() if s else None
            log("%-22s плашка: слот(T=%.2f H=%.2f)" % (pn, o.top if o else -1, o.bottom if o else -1))

    def take_shot(tag):
        cmd("shot showui")
        st["_shot_tag"] = tag

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    STEPS = [
        (s_travel, TRAVEL_SETTLE),
        (s_open, SETTLE),
        (s_settext, SETTLE),
        (s_metrics, SETTLE),
        (lambda: take_shot("asset"), SETTLE),
        (lambda: harvest("asset"), SETTLE),
        (s_align, SETTLE),
        (s_metrics, SETTLE),
        (lambda: take_shot("aligned"), SETTLE),
        (lambda: harvest("aligned"), SETTLE),
    ]

    def stop():
        if st["handle"] is not None:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        log("=== ГОТОВО ===")
        les.editor_request_end_play()
        try:
            unreal.SystemLibrary.quit_editor()
        except Exception:
            pass

    def on_tick(dt):
        st["tick"] += 1
        if st["busy"]:
            return
        if st["tick"] > MAX_TICKS:
            stop()
            return
        if st["tick"] < st["next_at"]:
            return
        st["step"] += 1
        if st["step"] >= len(STEPS):
            stop()
            return
        fn, wait = STEPS[st["step"]]
        st["busy"] = True
        try:
            fn()
        except Exception as e:
            log("!! шаг %d упал: %s" % (st["step"], e))
        st["busy"] = False
        st["next_at"] = st["tick"] + wait

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
