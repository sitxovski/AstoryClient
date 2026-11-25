# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_gacha_live_composite.py — ЖИВОЙ кадр композита премиум-гачи (PIE) + материал кадра.
#
# ★★★ ЗАЧЕМ. Все прежние зонды снимали РЕНДЕР-ТАРГЕТ и считали композит ОФФЛАЙН — там всё было
#   верно, а владелец в PIE видел светло-серые тени и бледную картинку. Значит ломался
#   ПОСЛЕДНИЙ шаг: материал кисти `M_AsdaGachaScene3D` + наложение в Slate. Этот зонд
#   единственный, кто снимает РЕЗУЛЬТАТ ЭТОГО ШАГА — то, что реально попадает на экран.
#
# ★★★ КАК ДОКАЗЫВАЕТСЯ ЧИСЛОМ (не «на глаз»).
#   Живой кадр = композит сцены НА ФОНЕ ОКНА, а эталонный расчёт владельца сделан на плоском
#   фоне 190. Сравнивать их в лоб нельзя. Поэтому снимаются ТРИ вещи одного и того же
#   застывшего кадра:
#     A) `live_scene.png`  — экран как есть (сцена поверх окна);
#     B) `live_bg.png`     — тот же экран при `OpacityGain = 0` у кисти, то есть ТОЛЬКО фон;
#     C) `live_rt.png`     — сам render-target (RGB предумножен, A = обратная непрозрачность).
#   Тогда предсказание эталонной формулы на ИЗМЕРЕННОМ фоне:
#       op = 1 − A/255;  direct = clip(RGB / max(op, 1/255), 0, 255);  pred = direct·op + B·(1−op)
#   и `pred` обязан совпасть с `A` с точностью до единиц байт. Если совпал — Slate делает
#   ровно то, что посчитал владелец, и картинка на экране = эталон.
#
# ★ МИР ЗАМОРАЖИВАЕТСЯ (`pause`) до съёмки: иначе клип покоя сдвинет сцену между кадрами A/B/C,
#   и сравнение будет мерить анимацию, а не композит. Совпадение A1 и A2 (две съёмки подряд)
#   печатается в лог как ДОКАЗАТЕЛЬСТВО заморозки.
#
# ЗАПУСК: Tools\run_headless_rhi_tick.bat @@ASTORY_DEV_ROOT@@\AstoryClient\Tools\asda_gacha_live_composite.py
#   (именно `_tick`: без тика редактора PIE не крутит кадры и скриншотов не будет).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/gacha_3d/live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/Login"

WARMUP = 240        # прогрев PIE: шейдеры, стриминг, GameMode
AFTER_OPEN = 180    # окно гачи открылось — дать сцене собраться и капчеру снять кадр
SETTLE = 45         # между съёмками
MAX_TICKS = 20000


def log(m):
    import unreal
    unreal.log("[гача-живой] " + str(m))


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
    log("уровень %s загружен" % LEVEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False,
          "sub": None, "act": None, "mid": None, "shots": []}

    def game_world():
        try:
            w = ues.get_game_world()
            if w:
                return w
        except Exception:
            pass
        return None

    def take_shot(tag):
        """Снимок вьюпорта ВМЕСТЕ С UI. `shot showui` = `FScreenshotRequest::RequestScreenshot`
        с `bShowUI = true` (`GameViewportClient.cpp:4284-4306`); HighResShot интерфейс НЕ пишет."""
        w = game_world()
        unreal.SystemLibrary.execute_console_command(w, "shot showui")
        st["shots"].append(tag)
        log("запрошен снимок '%s' на тике %d" % (tag, st["tick"]))

    def harvest(tag):
        """Забрать самый свежий PNG из Saved/Screenshots и переложить под своим именем."""
        pngs = glob.glob(os.path.join(SS_DIR, "**", "*.png"), recursive=True)
        if not pngs:
            log("⚠ снимок '%s' НЕ появился" % tag)
            return False
        pngs.sort(key=os.path.getmtime)
        dst = os.path.join(OUT_DIR, "%s.png" % tag)
        shutil.copy(pngs[-1], dst)
        os.remove(pngs[-1])
        log("снимок '%s' -> %s (%d байт)" % (tag, dst, os.path.getsize(dst)))
        return True

    # ── шаги машины состояний ───────────────────────────────────────────────────────────────
    def s_open():
        w = game_world()
        if not w:
            raise RuntimeError("нет игрового мира — PIE не стартовал")
        # ★ Подсистема берётся ЖИВАЯ из PIE, а не создаётся зондом: проверяем ТОТ ЖЕ путь,
        #   которым пользуется владелец. Дотянуться до неё из Python НЕЧЕМ
        #   (`unreal.SubsystemBlueprintLibrary` в обёртке отсутствует — проверено запуском),
        #   поэтому зовём консольную команду `asda.Gacha.Open`
        #   (`AsdaPremiumGachaSubsystem.cpp`, паттерн `asda.Booster.Open`).
        unreal.SystemLibrary.execute_console_command(w, "asda.Gacha.Open")
        log("asda.Gacha.Open отправлена")

    def s_bind():
        w = game_world()
        acts = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaGachaSceneActor)
        if not acts:
            raise RuntimeError("AAsdaGachaSceneActor в PIE не найден — сцена не создана")
        act = acts[0]
        st["act"] = act
        rt = act.get_render_target()
        mid = act.get_scene_brush_material()
        st["mid"] = mid
        log("актор=%s RT=%s материал=%s" % (act.get_name(), rt, mid))
        if rt is None or mid is None:
            raise RuntimeError("RT или MID не готовы")
        # ★★★ ФАКТ, А НЕ ВЕРА: формат RT и его признак sRGB. Именно от него зависит, декодирует
        #   ли сэмплер гамму сам (тогда узел sRGBToLinear в материале ЛИШНИЙ) или нет.
        #   `override_format` из Python НЕ читается (проверено запуском: «Failed to find property»),
        #   поэтому полный набор — включая `IsSRGB()` и `GetDisplayGamma()` — печатает C++ ниже.
        log("RT: формат=%s размер=%sx%s"
            % (rt.get_editor_property("render_target_format"),
               rt.get_editor_property("size_x"), rt.get_editor_property("size_y")))
        # ★★★ Тинт и прозрачность ВСЕЙ цепочки виджетов под кистью — печатает C++
        #   (`asda.Gacha.BrushDump`): из Python `FSlateBrush::UVRegion` и `RenderOpacity`
        #   родителей не читаются, а любая альфа < 1 на цепочке выцветит картинку.
        unreal.SystemLibrary.execute_console_command(w, "asda.Gacha.BrushDump")
        # Заморозка мира: иначе клип покоя сдвинет сцену между кадрами A / B / C.
        unreal.SystemLibrary.execute_console_command(w, "pause")
        log("мир поставлен на паузу")

    def s_shot_a1():
        take_shot("live_scene")

    def s_get_a1():
        harvest("live_scene")

    def s_shot_a2():
        take_shot("live_scene2")

    def s_get_a2():
        harvest("live_scene2")
        # RT снимается ТЕМ ЖЕ кадром (мир на паузе) — экспорт в PNG рядом со снимками.
        rt = st["act"].get_render_target()
        unreal.RenderingLibrary.export_render_target(game_world(), rt, OUT_DIR, "live_rt.png")
        p = os.path.join(OUT_DIR, "live_rt.png")
        log("RT выгружен: %s (%s)" % (p, os.path.getsize(p) if os.path.exists(p) else "НЕТ"))

    def s_bg_off():
        # ★ Фон окна замеряется тем же экраном: гасим ТОЛЬКО сцену параметром самого материала.
        st["mid"].set_scalar_parameter_value("OpacityGain", 0.0)
        log("OpacityGain = 0 (сцена погашена, остался фон окна)")

    def s_shot_b():
        take_shot("live_bg")

    def s_get_b():
        harvest("live_bg")
        st["mid"].set_scalar_parameter_value("OpacityGain", 1.0)

    PLAN = [s_open, s_bind, s_shot_a1, s_get_a1, s_shot_a2, s_get_a2,
            s_bg_off, s_shot_b, s_get_b]
    DELAYS = [AFTER_OPEN, SETTLE, SETTLE, SETTLE, SETTLE, SETTLE, SETTLE, SETTLE, SETTLE]

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    finish = PH.make_pie_finish(st, les, log, "=== готово, снимков запрошено %d ===", handle_is_not_none=True, count_shots=True)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[гача-живой] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            PLAN[st["step"]]()
            st["next_at"] = st["tick"] + DELAYS[st["step"]]
        except Exception:
            import traceback
            unreal.log_error("[гача-живой] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
