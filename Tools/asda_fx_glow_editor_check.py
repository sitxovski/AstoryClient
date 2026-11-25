# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_fx_glow_editor_check.py — ПРОВЕРКА В ЖИВОМ РЕДАКТОРЕ (не -game): категория `Effect Glow`
#   в панели деталей и у АКТОРА НА КАРТЕ, и у АССЕТА BP_Fx_*.
#
#   Что делает:
#     1. открывает карту, находит эффект-актор и ПЕЧАТАЕТ его сохранённые числа — это доказательство,
#        что переделка категории не потеряла значения У АКТОРОВ КАРТ (у ассетов доказано отдельным дампом);
#     2. выделяет актор (панель деталей показывает его свойства) и снимает ОКНО SLATE командой
#        `HighResShot` — именно в редакторе она отдаёт окно целиком, вместе с панелью деталей;
#     3. снимает КАДР ВЬЮПОРТА через `unreal.AutomationLibrary.take_high_res_screenshot()` (требование
#        владельца: HighResShot в редакторе отдаёт Slate, а не вьюпорт);
#     4. открывает ассет BP_Fx_* в редакторе блюпринтов и снимает окно ещё раз — категория обязана быть
#        и там (панель деталей CDO).
#   ★ Ничего не пишет: только читает, выделяет и снимает.
#
#   Запуск (редактор, БЕЗ -NullRHI, СКРЫТО):
#     UnrealEditor-Cmd.exe <uproject> -RenderOffscreen -ExecCmds="py <этот файл>" ...
import os, io, json, glob, shutil, time

OUTD = os.environ.get("ASDA_GLOW_OUTD", r"@@ASTORY_DEV_ROOT_FWD@@/Reports/fx_glowui")
MAP = os.environ.get("ASDA_GLOW_MAP", "/Game/Map/alpen/alpen")
# ★ D5 (волна 2 п. 7, 14.09.2026, сверка с Content): умолчание УСТАРЕЛО — BP_Fx_mdel_roadlamp02-a_00 нет ни с «-», ни с «_», и BP_Fx_*roadlamp* в Content нет
#   вовсе. Подставлять похожий ассет нельзя (не выдумывать) — задавайте ASDA_GLOW_BP явно.
BPASSET = os.environ.get("ASDA_GLOW_BP", "/Game/AsdaAssets/Effects/BP_Fx_mdel_roadlamp02-a_00")
RES = os.environ.get("ASDA_GLOW_RES", "1920x1080")
SSDIRS = [r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/WindowsEditor",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots/Windows",
          r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"]

FIELDS = ["b_enabled", "b_manual", "family_id", "intensity_cd", "radius_min", "radius_max",
          "color_mode", "color_saturation", "b_override_emissive", "emissive_boost"]
ALIASES = {"b_enabled": ["b_enabled", "enabled"], "b_manual": ["b_manual", "manual"],
           "b_override_emissive": ["b_override_emissive", "override_emissive"]}


def main():
    import unreal
    R = {"карта": MAP, "ассет": BPASSET, "кадры": {}, "акторы": [], "заметки": []}
    st = {"n": 0, "t": 0.0, "q": [], "cursor": 0.0, "shot_t0": 0.0}
    rx, ry = [int(v) for v in RES.lower().split("x")]

    def log(m):
        unreal.log("[glowed] " + str(m))

    def at(secs, fn):
        st["q"].append([secs, fn])

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    run_queue = PH.make_time_queue_runner(st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 7 стр.)

    newest_png = PH.make_newest_png(SSDIRS)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 8 стр.)

    def collect(nm):
        src = newest_png(st["shot_t0"])
        if not src:
            R["заметки"].append("КАДР НЕ ПОЯВИЛСЯ: " + nm)
            log("КАДР НЕ ПОЯВИЛСЯ: " + nm)
            return
        dst = os.path.join(OUTD, "glowed_%s.png" % nm)
        shutil.copy(src, dst)
        R["кадры"][nm] = dst
        log("СОХРАНЁН -> " + dst)

    def snap(fx):
        d = {}
        for f in FIELDS:
            for nm in ALIASES.get(f, [f]):
                try:
                    v = fx.get_editor_property(nm)
                    d[f] = round(v, 4) if isinstance(v, float) else (v if isinstance(v, (bool, int, str)) else str(v))
                    break
                except Exception:
                    continue
        return d

    def phase_level():
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        les.load_level(MAP)
        log("уровень открыт: %s" % MAP)
        at(st["t"] + 6.0, phase_select)

    def phase_select():
        eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        target = None
        for a in eas.get_all_level_actors():
            if a.get_class().get_name() != "AsdaEffectActor":
                continue
            fx = a.get_editor_property("fx_light")
            row = {"актор": a.get_name(), "эффект": a.get_editor_property("effect_name"), "поля": snap(fx)}
            if len(R["акторы"]) < 6:
                R["акторы"].append(row)
                log("АКТОР КАРТЫ %s (%s): %s" % (row["актор"], row["эффект"], json.dumps(row["поля"], ensure_ascii=False)))
            if target is None and row["поля"].get("family_id"):
                target = a
        if target is None:
            R["заметки"].append("эффект-актор на карте не найден")
            at(st["t"] + 0.5, phase_asset)
            return
        eas.set_selected_level_actors([target])
        log("выделен актор %s — панель деталей показывает его свойства" % target.get_name())
        at(st["t"] + 3.0, phase_shot_slate)

    def phase_shot_slate():
        st["shot_t0"] = time.time() - 1.0
        # ★ В РЕДАКТОРЕ HighResShot отдаёт ОКНО SLATE целиком — здесь это и нужно: панель деталей.
        unreal.SystemLibrary.execute_console_command(None, "HighResShot %dx%d" % (rx, ry))
        log("запрошен снимок окна редактора (Slate)")
        at(st["t"] + 5.0, lambda: collect("details_level"))
        at(st["t"] + 6.0, phase_shot_viewport)

    def phase_shot_viewport():
        st["shot_t0"] = time.time() - 1.0
        # ★ Требование владельца: кадр ВЬЮПОРТА — через AutomationLibrary.
        unreal.AutomationLibrary.take_high_res_screenshot(rx, ry, "glowed_viewport")
        log("запрошен кадр вьюпорта (AutomationLibrary)")
        at(st["t"] + 6.0, lambda: collect("viewport"))
        at(st["t"] + 7.0, phase_asset)

    def phase_asset():
        try:
            obj = unreal.load_asset(BPASSET)
            unreal.AssetToolsHelpers.get_asset_tools().open_editor_for_assets([obj])
            log("открыт редактор ассета %s" % BPASSET)
            cdo = unreal.get_default_object(obj.generated_class())
            log("АССЕТ %s: %s" % (BPASSET, json.dumps(snap(cdo.get_editor_property("fx_light")), ensure_ascii=False)))
        except Exception as ex:
            R["заметки"].append("ассет не открылся: %s" % ex)
        at(st["t"] + 6.0, phase_shot_asset)

    def phase_shot_asset():
        st["shot_t0"] = time.time() - 1.0
        unreal.SystemLibrary.execute_console_command(None, "HighResShot %dx%d" % (rx, ry))
        log("запрошен снимок окна редактора ассета")
        at(st["t"] + 5.0, lambda: collect("details_asset"))
        at(st["t"] + 6.0, finish)

    def finish():
        p = os.path.join(OUTD, "glow_editor_check.json")
        with io.open(p, "w", encoding="utf-8") as fh:
            json.dump(R, fh, ensure_ascii=False, indent=1)
        log("записан " + p)
        log("GLOW-EDITOR ЗАВЕРШЕНО")

    def tick(dt):
        st["n"] += 1
        st["t"] += float(dt)
        if st["n"] == 1:
            at(st["t"] + 2.0, phase_level)
            return
        try:
            run_queue()
        except Exception as ex:
            import traceback
            unreal.log_error("[glowed] ошибка шага: %s\n%s" % (ex, traceback.format_exc()))

    unreal.register_slate_post_tick_callback(tick)
    log("старт проверки в редакторе")


# ★ Закон tools-no-import-side-effects (гигиена 15.09.2026): при ИМПОРТЕ main() не запускается.
#   Прямое исполнение (python file.py; UE -ExecutePythonScript / py / -run=pythonscript; exec с
#   __name__="__main__") — запускает: там __name__ == "__main__" и __spec__ отсутствует.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
