# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_quest_reward_tip_exp_check.py — ЖИВАЯ (PIE) проверка ДВУХ правок захода 2026-08-23:
#
#   1) ПОДСКАЗКА ПРЕДМЕТА в попапе наград `EPISODEPOP_WIN` (540) — порт msg 8 эталона
#      (`uiWindowMsgHandler@0xD91DB0` → `uiShowEquipSlotTooltip2@0xD925A0`). Проверяются ОБА блока:
#      «Обычная награда» (иконки 544…2401) и «Дополнительная награда» (557…2404).
#      ★ Мышью на иконку 32×32 в headless-PIE не навести, поэтому показ вызывается командой
#        `asda.Npc.RewardTip <панель> <слот>` — она зовёт ТО ЖЕ ТЕЛО (`ShowRewardSlotTip`), что и
#        покадровый опрос курсора. Отдельной тестовой ветки показа НЕТ.
#
#   2) ЯКОРЬ ВСПЛЫВАЮЩЕЙ НАДПИСИ «ОПЫТ+» над СВОИМ персонажем. Прежний якорь брался из
#      `GetActorBounds(false)`, а у `AAsdaPlayerCharacter` в габариты входят `USpringArmComponent`
#      (700…1270 юнитов) и камера на его конце — «центр актора» уезжал за спину. Поэтому павном
#      здесь стоит НАСТОЯЩИЙ `AsdaPlayerCharacter` (а не `DefaultPawn`, как в
#      `asda_exp_gain_live_check.py`): именно на нём дефект и проявлялся у владельца.
#      Числа даёт `asda.Fx.FloatDump` (экранная позиция каждой живой надписи), картинку — снимок.
#
# ЗАПУСК: Tools\run_headless_quest_reward_tip_exp.bat
#   ★ Нужен ТИК редактора (`-ExecCmds="py …"`) и РЕНДЕР (без `-NullRHI`).
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/quest_reward_tip_exp"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"
PAWN_LABEL = "TipExpProbePawn"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 40
SHORT = 6            # между «показать надпись» и «снять кадр»: пик масштаба на 50…90 мс
MAX_TICKS = 40000

PAWN_LOC = (0.0, 0.0, 30000.0)   # высоко над картой: чистое небо за надписью


def log(m):
    import unreal
    unreal.log("[награда-подсказка+опыт] " + str(m))


def main():
    import unreal

    if os.path.isdir(SS_DIR):
        shutil.rmtree(SS_DIR, ignore_errors=True)
    if os.path.isdir(OUT_DIR):
        shutil.rmtree(OUT_DIR, ignore_errors=True)
    os.makedirs(OUT_DIR)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    unreal.EditorLoadingAndSavingUtils.load_map(LEVEL)
    log("уровень %s загружен" % LEVEL)
    # ★ Павн кладём в РЕДАКТОРСКИЙ уровень — PIE его продублирует (приём из exp_gain_live_check:
    #   спавнить прямо в PIE-мир из Python нельзя, EditorActorSubsystem работает с редакторским).
    pawn_probe = eas.spawn_actor_from_class(unreal.AsdaPlayerCharacter, unreal.Vector(*PAWN_LOC),
                                            unreal.Rotator(0, 0, 0))
    pawn_probe.set_actor_label(PAWN_LABEL)
    log("павн-зонд AsdaPlayerCharacter «%s» добавлен" % PAWN_LABEL)
    les.editor_request_begin_play()
    log("PIE запущен")

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None, "busy": False, "shots": []}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    take_shot = PH.make_take_shot(cmd, st)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 3 стр.)

    harvest = PH.make_harvest(SS_DIR, OUT_DIR, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 10 стр.)

    def possess_pawn():
        """Найти павн-зонд в PIE-мире, собрать ему внешность и вселить контроллер.

        Внешность обязательна: якорь надписи считается по ВИДИМЫМ меш-компонентам
        (`UAsdaWorldSubsystem::VisibleMeshBounds`), а без `InitAppearance` у павна нет ни одного
        меша — проверялся бы запасной путь, а не исправленный.
        """
        w = world()
        pc = unreal.GameplayStatics.get_player_controller(w, 0)
        if not pc:
            raise RuntimeError("нет AsdaPlayerController — режим не поднялся")
        pawn = None
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.AsdaPlayerCharacter):
            pawn = a
            break
        if not pawn:
            raise RuntimeError("павн-зонд не продублировался в PIE-мир")
        pawn.set_actor_location(unreal.Vector(*PAWN_LOC), False, False)
        try:
            pawn.init_appearance(1, unreal.AsdaWornParts())
            log("внешность павна собрана (пол=1, части по умолчанию)")
        except Exception as e:
            log("⚠ init_appearance не отработал: %s" % e)
        pc.possess(pawn)
        log("павн %s вселён" % pawn.get_name())

    # ── план: (действие, пауза после него в тиках) ──────────────────────────────────────────────
    PLAN = [
        (lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        (lambda: cmd("asda.Msg.Verbose 1"), 5),
        (possess_pawn, SETTLE),

        # ══ 1. НАДПИСЬ «ОПЫТ+» НАД СВОИМ ПЕРСОНАЖЕМ (ветка A эталона, flag=1) ══
        (lambda: cmd("asda.Hud.TestExpGain 1395 1"), SHORT),
        (lambda: cmd("asda.Fx.FloatDump"), 2),
        (lambda: take_shot("exp_over_player"), SETTLE),
        (lambda: harvest("exp_over_player"), 5),

        # ══ 2. ПОДСКАЗКА ПО ИКОНКЕ «ОБЫЧНОЙ НАГРАДЫ» ══
        #   `149npc talkId=11 шаг 0` — гарантированная награда с ПРЕДМЕТОМ (код 30985, вид 8).
        #   Тройка взята из данных `RawAssets/Data/NPC/NpcNsrPage.json` (поле A52 > 0), не придумана.
        (lambda: cmd("asda.Npc.RewardShow 149npc 11 0 0"), SETTLE),
        (lambda: cmd("asda.Npc.RewardDump"), 5),
        (lambda: cmd("asda.Npc.RewardTip 0 0"), 3),
        (lambda: cmd("asda.Npc.RewardTip 0 1"), 3),
        (lambda: cmd("asda.Npc.RewardTip 0 2"), 3),
        (lambda: cmd("asda.ItemTip.State"), 3),
        (lambda: take_shot("reward_tip_basic"), SETTLE),
        (lambda: harvest("reward_tip_basic"), 5),

        # ══ 3. ПОДСКАЗКА ПО ИКОНКЕ «ДОПОЛНИТЕЛЬНОЙ НАГРАДЫ» ══
        #   `117npc talkId=7 шаг 0` — режим 1: гарантированные деньги+опыт и ДВА выбираемых предмета.
        (lambda: cmd("asda.Npc.RewardShow 117npc 7 0 0"), SETTLE),
        (lambda: cmd("asda.Npc.RewardTip 1 0"), 3),
        (lambda: cmd("asda.ItemTip.State"), 3),
        (lambda: take_shot("reward_tip_select"), SETTLE),
        (lambda: harvest("reward_tip_select"), 5),
        # ★ Гарантированная панель здесь — деньги+опыт (виды 0 и 1): у эталона подсказки НЕТ,
        #   и команда обязана сказать это строкой, а не показать плашку.
        (lambda: cmd("asda.Npc.RewardTip 0 0"), 3),
        (lambda: cmd("asda.Npc.RewardTip 0 1"), 3),

        # ══ 4. НАДПИСЬ ПРИ ОТКРЫТОМ ПОПАПЕ (проверка перекрытия: эталон рисует текст ДО окон) ══
        (lambda: cmd("asda.Hud.TestExpGain 1395 1"), SHORT),
        (lambda: cmd("asda.Fx.FloatDump"), 2),
        (lambda: take_shot("exp_under_reward_popup"), SETTLE),
        (lambda: harvest("exp_under_reward_popup"), 5),
    ]

    def finish():
        if st["handle"]:
            unreal.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        try:
            eas.destroy_actor(pawn_probe)   # уровень НЕ сохраняем, зонд убираем
        except Exception:
            pass
        log("=== готово, снимков запрошено %d ===" % len(st["shots"]))
        unreal.SystemLibrary.quit_editor()

    def on_tick(_dt):
        if st["busy"]:
            return
        st["busy"] = True
        try:
            st["tick"] += 1
            if st["tick"] > MAX_TICKS:
                unreal.log_error("[награда-подсказка+опыт] предел тиков")
                finish()
                return
            if st["tick"] < st["next_at"]:
                return
            st["step"] += 1
            if st["step"] >= len(PLAN):
                finish()
                return
            fn, wait = PLAN[st["step"]]
            fn()
            st["next_at"] = st["tick"] + wait
        except Exception:
            import traceback
            unreal.log_error("[награда-подсказка+опыт] ошибка:\n" + traceback.format_exc())
            finish()
        finally:
            st["busy"] = False

    st["handle"] = unreal.register_slate_post_tick_callback(on_tick)
    log("tick-callback зарегистрирован")


# ★ ЗАКОН tools-no-import-side-effects: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
