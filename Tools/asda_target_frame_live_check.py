# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_target_frame_live_check.py — ЖИВАЯ (PIE) проверка ПАНЕЛИ ЦЕЛИ (JUI PVP_TARGET, idnum 40).
#
# ★★★ ЗАЧЕМ ЖИВАЯ (закон `false-negatives-method`): офлайн-дамп .uasset показывает только
#   разметку. Поднялась ли подсистема, нашлись ли все 228 узлов, легли ли значения на полосы,
#   цифры и портрет — видно только в запущенном мире.
#
# ЧТО ДЕЛАЕТ:
#   1) травел на карту с мобами;
#   2) `asda.Target.Dump` БЕЗ цели — печатает состав окна (окно поднимается скрытым);
#   3) `asda.Target.Pick` — выбирает целью ближайшего РЕАЛЬНОГО моба уровня (не выдуманная цель:
#      у актора настоящие поля типа — уровень, агрессия/ранг, стихия, портрет, «жизни»);
#   4) `asda.Target.Dump` с целью + снимок экрана;
#   5) перебор ключей отката (legacy-режимы) с дампом и снимком на каждый.
#
# ЗАПУСК:
#   python Tools/run_headless_safe.py Tools/asda_target_frame_live_check.py \
#       --exec-cmds --log @@ASTORY_DEV_ROOT_FWD@@/Reports/target_frame_live.log
import glob
import os
import shutil

OUT_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/Reports/target_frame_live"
SS_DIR = r"@@ASTORY_DEV_ROOT_FWD@@/AstoryClient/Saved/Screenshots"
LEVEL = "/Game/Map/alpen/alpen"

WARMUP = 300
TRAVEL_SETTLE = 400
SETTLE = 90
MAX_TICKS = 40000

# Узлы окна, которые обязаны существовать в живом дереве (по одному представителю на группу
#   контролов разбора `Docs/UI/HUD/PVP_TARGET.md`).
NODES = [
    "PVP_TARGET00_grp", "PVP_TARGET00_10_0",
    "Target_Gear_Normal_2_0", "PVP_TARGETNAMED_3_0", "PVP_TARGETElite_4_0", "PVP_TARGETBOSS_5_0",
    "Target_Gear_NormalAni_6_0", "PVP_TARGETNamedAni_7_0", "PVP_TARGETEliteAni_8_0", "PVP_TARGETBossAni_9_0",
    "PVP_LVTxtBase_grp", "PVP_LVTXT", "PVP_Count0_13_0", "PVP_Count9_22_0",
    "PVP_TARGETNAME", "PVP_TARGETNAME_23_0",
    "PVP_HPBAR_29_0", "PVP_HPBAR_Y_30_0", "PVP_HPBAR_Y_30_1", "PVP_HPBAR_G_31_0", "PVP_HPBAR_P_32_0",
    "PVP_MPBAR_28_0", "PVP_MPBAR_HIDE_33_0", "PVP_HPBuff_26_0", "PVP_MPBuff_27_0",
    "PVP_IMAGE_43_0", "PVP_BEING_ATTACK_44_0", "PVP_BAT_FIRST_MARK_112_0",
    "PVP_ELE_Base_grp", "PVP_MARK00_46_0", "PVP_MARK05_51_0",
    "PVP_ClanMarkBase_grp", "PVP_ClanMark_53_0",
    "ENERMY_STATE_CONTROLWIN_grp", "ENERMY_STATE_ICON00_grp", "ENERMY_STATE_ICON27_grp",
]


def log(m):
    import unreal
    unreal.log("[панель-цели-живой] " + str(m))


def main():
    import unreal

    # ★ ГРАБЛЯ (живой прогон 23.08.2026): при запуске через `-ExecCmds=py …` python-биндинги UMG
    #   ещё не сгенерированы, и `unreal.WidgetBlueprintLibrary` отсутствует — все виджет-дампы
    #   молча падали в «окна нет». Грузим модуль явно.
    for m in ("UMG", "UMGEditor"):
        try:
            unreal.load_module(m)
        except Exception as e:
            unreal.log_warning("[панель-цели-живой] load_module(%s): %s" % (m, e))

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

    st = {"tick": 0, "step": -1, "next_at": WARMUP, "handle": None}

    import sys as _ph_sys, os as _ph_os   # ★ волна 3 п.15: общий каркас живых проверок (Tools/asda_pie_harness.py)
    _ph_dir = _ph_os.path.dirname(_ph_os.path.abspath(__file__))   # UE не кладёт папку скрипта в sys.path
    if _ph_dir not in _ph_sys.path: _ph_sys.path.insert(0, _ph_dir)
    import asda_pie_harness as PH
    world = PH.make_game_world(ues)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 5 стр.)

    cmd = PH.make_console_cmd(world, log)   # ★ волна 3 п.15: тело 1:1 в asda_pie_harness (было 6 стр.)

    def win():
        w = world()
        if not w:
            return None
        try:
            all_w = unreal.WidgetBlueprintLibrary.get_all_widgets_of_class(w, unreal.UserWidget, False)
        except Exception as e:
            log("!! get_all_widgets_of_class: %s" % e)
            return None
        for uw in all_w:
            if "WBP_PVP_TARGET" in uw.get_class().get_name():
                return uw
        log("!! окно WBP_PVP_TARGET среди %d виджетов не найдено" % len(all_w))
        return None

    def dump_nodes(tag):
        """Проверка «контролы находятся по именам» + печать их живого состояния."""
        log("── ЖИВЫЕ УЗЛЫ ОКНА (%s) ──" % tag)
        uw = win()
        if not uw:
            log("(окно не поднято; состав печатает сам движок — см. asda.Target.Dump выше)")
            return
        miss = []
        for n in NODES:
            w2 = uw.get_widget_from_name(n)
            if not w2:
                miss.append(n)
                continue
            try:
                vis = str(w2.get_visibility()).split(".")[-1]
            except Exception:
                vis = "?"
            extra = ""
            try:
                br = w2.get_editor_property("brush")
                res = br.get_editor_property("resource_object")
                extra = " браш=%s" % (res.get_name() if res else "НЕТ")
            except Exception:
                pass
            try:
                extra += " текст=%r" % str(w2.get_text())
            except Exception:
                pass
            log("%-30s %-18s vis=%-22s%s" % (n, w2.get_class().get_name(), vis, extra))
        if miss:
            log("!! УЗЛОВ НЕ НАЙДЕНО (%d): %s" % (len(miss), miss))
        else:
            log("все %d проверяемых узлов найдены" % len(NODES))

    def take_shot(tag):
        cmd("shot showui")
        st["_shot_tag"] = tag

    def harvest(tag):
        pngs = glob.glob(os.path.join(SS_DIR, "**", "*.png"), recursive=True)
        if not pngs:
            log("⚠ снимок '%s' НЕ появился" % tag)
            return
        pngs.sort(key=os.path.getmtime)
        dst = os.path.join(OUT_DIR, "%s.png" % tag)
        shutil.copy2(pngs[-1], dst)
        log("снимок '%s' → %s (%d Б)" % (tag, dst, os.path.getsize(dst)))

    STEPS = [
        ("travel",       lambda: cmd("open /Game/Map/alpen/alpen?game=/Script/AsdaReborn.AsdaWorldGameMode"), TRAVEL_SETTLE),
        # 1. Состав окна БЕЗ цели (окно поднимается скрытым — это и проверяем).
        ("состав-0",     lambda: cmd("asda.Target.Dump"), 30),
        ("узлы-0",       lambda: dump_nodes("без цели"), 10),
        # 2. Выбор реального моба целью.
        ("выбор",        lambda: cmd("asda.Target.Pick"), SETTLE),
        ("состав-1",     lambda: cmd("asda.Target.Dump"), 30),
        ("узлы-1",       lambda: dump_nodes("цель выбрана"), 10),
        ("дерево-1",     lambda: cmd("asda.Target.Nodes"), 40),
        ("видимые-1",    lambda: cmd("asda.Target.Nodes vis"), 40),
        ("снимок-1",     lambda: take_shot("target_mob"), SETTLE),
        ("сбор-1",       lambda: harvest("target_mob"), 5),
        # 3. Ключи отката: проверяем, что каждое отступление реально откатывается.
        ("legacy-ele",   lambda: cmd("asda.Target.LegacyEleBase 1"), 5),
        ("legacy-ba",    lambda: cmd("asda.Target.LegacyBeingAttack 1"), 5),
        ("legacy-lvl",   lambda: cmd("asda.Target.LegacyLevelColor 1"), 5),
        ("legacy-mark",  lambda: cmd("asda.Target.LegacyElementMark 1"), 5),
        ("legacy-центр", lambda: cmd("asda.Target.CenterByPanel 0"), 5),
        ("legacy-обн",   lambda: cmd("asda.Target.Refresh"), SETTLE),
        ("состав-2",     lambda: cmd("asda.Target.Dump"), 30),
        ("снимок-2",     lambda: take_shot("target_mob_legacy"), SETTLE),
        ("сбор-2",       lambda: harvest("target_mob_legacy"), 5),
        # 4. Возврат в режим порта + полоса MP цели (отступление, по умолчанию выключено).
        ("норм-ele",     lambda: cmd("asda.Target.LegacyEleBase 0"), 5),
        ("норм-ba",      lambda: cmd("asda.Target.LegacyBeingAttack 0"), 5),
        ("норм-lvl",     lambda: cmd("asda.Target.LegacyLevelColor 0"), 5),
        ("норм-mark",    lambda: cmd("asda.Target.LegacyElementMark 0"), 5),
        ("норм-центр",   lambda: cmd("asda.Target.CenterByPanel 1"), 5),
        ("mp-вкл",       lambda: cmd("asda.Target.ShowMp 1"), 5),
        ("норм-обн",     lambda: cmd("asda.Target.Refresh"), SETTLE),
        ("состав-3",     lambda: cmd("asda.Target.Dump"), 30),
        ("снимок-3",     lambda: take_shot("target_mob_port"), SETTLE),
        ("сбор-3",       lambda: harvest("target_mob_port"), 5),
        ("mp-выкл",      lambda: cmd("asda.Target.ShowMp 0"), 5),
        ("конец",        lambda: None, 5),
    ]

    def tick(dt):
        st["tick"] += 1
        if st["tick"] < st["next_at"]:
            if st["tick"] > MAX_TICKS:
                log("!! таймаут")
                stop()
            return
        st["step"] += 1
        if st["step"] >= len(STEPS):
            stop()
            return
        name, fn, wait = STEPS[st["step"]]
        try:
            fn()
        except Exception as e:
            log("!! шаг '%s': %s" % (name, e))
        st["next_at"] = st["tick"] + wait

    def stop():
        import unreal as U
        if st["handle"]:
            U.unregister_slate_post_tick_callback(st["handle"])
            st["handle"] = None
        try:
            les.editor_request_end_play()
        except Exception:
            pass
        log("ЗАВЕРШЕНО")

    st["handle"] = unreal.register_slate_post_tick_callback(tick)


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
