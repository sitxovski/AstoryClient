# -*- coding: utf-8 -*-
# Copyright (c) 2026 @sitxovski (Discord)
# asda_rescale_login_scene.py — перевод СЦЕНЫ ЛОГИНА из масштаба ×100 в ×1 (часть, живущая в уровне).
#
# ★★★ ЗАЧЕМ (12.09.2026, жалоба владельца «на login-сцене масштабы огромные; Spot и Directional
#   не работают»). Сцена логина собиралась в ×100: 1 игровая единица = 100 юнитов UE. Причина —
#   glTF-импорт задника и шкатулки: glTF трактует единицы файла как МЕТРЫ и домножает на 100.
#   Логин оказался ЕДИНСТВЕННОЙ локацией порта не в ×1 — у всех 24 игровых карт
#   `asda_build_map_level.py SCALE = 1.0` (игровая единица = 1 см UE).
#   ЗАМЕР 12.09.2026 (Reports/login_scale/probe_scale.json):
#       медианный габарит элемента  логин 18 736 юн   alpen 179.7 юн   → ×104
#       превью персонажа            15 592 юн в высоту = 156 м вместо 1.56 м
#   Из-за этого ни одна длина-умолчание UE на логине ничего не покрывала: радиус затухания
#   точечного и КОНУСНОГО света 1000 юн = 1/18 одного листа папоротника, каскады теней 40 000 юн
#   при сцене 894 470 юн и камере в 139 690 юн от центра ⇒ теней нет вовсе.
#
# ЧТО ДЕЛАЕТ ЭТОТ СКРИПТ (только уровень; всё остальное — своими тулами, см. ниже):
#   1) шкатулка `LoginCasket_cre_creation01` — позиция ÷100 и масштаб ×0.01 (меш запечён в ×100,
#      переимпорт не делаем: эти меши делят системы одержимости/«души»);
#   2) камера `LoginCamera` — позиция ÷100;
#   3) `Sun_Login` — дальность динамических теней 40 000 → 20 000, как у Sun_<карта> на игровых картах
#      (в ×1-сцене 20 000 юн = 200 м покрывают весь логин, в ×100 не покрывали ничего).
#   Глифы `CharSelectGlyph_slot*` и `CharSelectEffect_*` ПРИСТЁГНУТЫ к костям шкатулки — их мировой
#   масштаб и позиция пересчитываются движком сами (отн. масштаб −85/85/85 × 0.01 = 0.85 = те же
#   0.85 игровой единицы, что и раньше). Поэтому их НЕ трогаем.
#
# ОСТАЛЬНЫЕ ЧАСТИ ПЕРЕВОДА (делаются отдельно, порядок важен):
#   • 697 элементов задника + 491 меш — `asda_build_backdrop_elements.py` (ASDA_LOGIN_SCALE=1);
#   • дельта-треки анимации фона — `asda_export_backdrop_anim.py` + `asda_build_backdrop_anim.py`;
#   • трек перелёта — значения в `AsdaLoginFlightTrack.inl` ÷100, затем
#     `asda_build_login_flight_sequence.py` (ПОСЛЕ этого скрипта: секвенция кеит масштаб шкатулки);
#   • рантайм-геометрия — `GLoginScale` в `AsdaLoginGameMode.cpp`;
#   • масштаб превью — `CharacterScale` 0.01 в `DA_LoginConfig` и в дефолтах C++;
#   • свет эффектов — `world_scales` логина в `Tools/asda_fx_light.json` (100 → 1) + перегенерация ini.
#
# ★★★ ИДЕМПОТЕНТНОСТЬ (правка 12.09.2026 по ревизии). Скрипт НЕ «множит длины на 0.01»: он читает
#   ФАКТИЧЕСКИЙ масштаб сцены из уровня и доводит его до ЦЕЛЕВОГО. Множитель вычисляется как
#   K = ЦЕЛЬ / ТЕКУЩЕЕ, поэтому повторный прогон даёт K = 1 и ничего не портит; если сцена уже в
#   целевом масштабе — скрипт выходит, НЕ СОХРАНЯЯ уровень. Прежняя версия множила текущее
#   состояние на константу и сразу сохраняла: второй прогон ужал бы сцену ещё в 100 раз.
#   Текущий масштаб читается по шкатулке: её меш запечён glTF-импортом в ×100, поэтому
#   «юнитов UE в одной игровой единице» = |масштаб актора шкатулки| × 100.
#
# Ключи:
#   ASDA_LOGIN_TARGET_SCALE — целевой масштаб сцены, юнитов UE в ОДНОЙ игровой единице (умолч. 1.0;
#                             откат к прежней сцене = 100.0). ДОЛЖЕН совпадать с GLoginScale в
#                             AsdaLoginGameMode.cpp и с ASDA_LOGIN_SCALE у тулов задника/глифов.
#   ASDA_LOGIN_RESCALE_FORCE=1 — выполнить, даже если сцена уже в целевом масштабе (K выйдет 1.0).
#   ASDA_LOGIN_SUN_SHADOW   — дальность динамических теней Sun_Login (умолч. 20000, как Sun_<карта>;
#                             это НЕ пропорция, а осознанная перенастройка: в ×100-сцене было 40000).
#   ASDA_LOGIN_SKY_THR      — sky_distance_threshold у Sky_Login (умолч. 1500 × цель; это ПРОПОРЦИЯ:
#                             в ×100-сцене стоял движковый дефолт 150 000).
#   ASDA_LOGIN_CHAR_SCALE   — CharacterScale в DA_LoginConfig (умолч. 0.01 × цель: меши превью
#                             запечены в ×100).
# Запуск (СКРЫТО, редактор закрыт):
#   python Tools/run_headless_safe.py Tools/asda_rescale_login_scene.py --marker RESCALE-FINISHED
import os
import unreal

MESH_BAKE = 100.0     # во сколько раз glTF-импорт запёк меши задника/шкатулки (метры→см)
TARGET = float(os.environ.get("ASDA_LOGIN_TARGET_SCALE", "1.0"))   # юнитов UE в одной игровой единице
FORCE = os.environ.get("ASDA_LOGIN_RESCALE_FORCE", "0") == "1"
SUN_SHADOW = float(os.environ.get("ASDA_LOGIN_SUN_SHADOW", "20000"))
SKY_THR = float(os.environ.get("ASDA_LOGIN_SKY_THR", str(1500.0 * TARGET)))
LEVEL = "/Game/Map/Login"
CFG = "/Game/AsdaAssets/Config/DA_LoginConfig"
CHAR_SCALE = float(os.environ.get("ASDA_LOGIN_CHAR_SCALE", str(0.01 * TARGET)))

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log_warning("[rescale] %s" % m)


def vs(v):
    return "(%.4f, %.4f, %.4f)" % (v.x, v.y, v.z)


def main():
    # ★★★ ЗАЩИТА СВЕТА ВЛАДЕЛЬЦА (13.09.2026): без ASDA_ALLOW_LOGIN_WRITE=1 — стоп до загрузки уровня
    import sys as _sys, os as _os
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    from asda_headless_tool import forbid_login_write   # ★ волна 3 п.15: каркас headless-инструмента (внутри — asda_login_guard, правило то же)
    forbid_login_write(__file__)   # имя файла = прежний литерал 'asda_rescale_login_scene.py'
    assert les.load_level(LEVEL), "не загрузился %s" % LEVEL
    by_label = {}
    for a in eas.get_all_level_actors():
        by_label.setdefault(a.get_actor_label(), a)
    changed = []

    casket = by_label.get("LoginCasket_cre_creation01")
    assert casket is not None, "в уровне нет LoginCasket_cre_creation01"

    # ★ ГВАРД: текущий масштаб сцены читаем из уровня, множитель выводим из него.
    cur_scale = abs(float(casket.get_actor_scale3d().x)) * MESH_BAKE
    assert cur_scale > 1e-9, "масштаб шкатулки нулевой — уровень повреждён, править нечего"
    log("масштаб сцены: сейчас %.4f юн/игр.ед., цель %.4f" % (cur_scale, TARGET))
    K = TARGET / cur_scale
    at_target = abs(cur_scale - TARGET) <= 1e-3 * TARGET
    # ★ Абсолютные (не пропорциональные) величины проверяем отдельно: сцена может быть уже в целевом
    #   масштабе, а порог неба или дальность теней — ещё нет (так и вышло 12.09: геометрию перевели
    #   первым прогоном, а sky_distance_threshold нашла ревизия).
    sky0 = by_label.get("Sky_Login")
    sun0 = by_label.get("Sun_Login")
    cfg0 = EAL.load_asset(CFG)
    todo = not at_target
    if sun0 is not None:
        todo = todo or abs(float(sun0.get_component_by_class(unreal.DirectionalLightComponent)
                                 .get_editor_property("dynamic_shadow_distance_movable_light")) - SUN_SHADOW) > 1e-3
    if sky0 is not None:
        todo = todo or abs(float(sky0.get_component_by_class(unreal.SkyLightComponent)
                                 .get_editor_property("sky_distance_threshold")) - SKY_THR) > 1e-3
    if cfg0 is not None:
        todo = todo or abs(float(cfg0.get_editor_property("character_scale")) - CHAR_SCALE) > 1e-9
    if not todo and not FORCE:
        log("сцена и её параметры УЖЕ в целевом состоянии — уровень НЕ трогаю и НЕ сохраняю. "
            "Повторить принудительно: ASDA_LOGIN_RESCALE_FORCE=1")
        log("RESCALE-FINISHED (без изменений)")
        return
    if at_target:
        log("геометрия уже в целевом масштабе (K = %.6f, то есть без движения) — правлю только параметры" % K)
    log("множитель длин K = цель / текущее = %.6f" % K)

    # 1) шкатулка: позиция ×K, масштаб ×K (знак зеркала по X сохраняем — он авторский)
    l0, s0 = casket.get_actor_location(), casket.get_actor_scale3d()
    casket.set_actor_location(unreal.Vector(l0.x * K, l0.y * K, l0.z * K), False, False)
    casket.set_actor_scale3d(unreal.Vector(s0.x * K, s0.y * K, s0.z * K))
    changed.append("шкатулка: loc %s -> %s, scale %s -> %s"
                   % (vs(l0), vs(casket.get_actor_location()), vs(s0), vs(casket.get_actor_scale3d())))

    # 2) камера логина: позиция ÷100 (поворот и FOV не трогаем — они безразмерны)
    cam = by_label.get("LoginCamera")
    assert cam is not None, "в уровне нет LoginCamera"
    c0 = cam.get_actor_location()
    cam.set_actor_location(unreal.Vector(c0.x * K, c0.y * K, c0.z * K), False, False)
    changed.append("камера: loc %s -> %s" % (vs(c0), vs(cam.get_actor_location())))

    # 3) солнце: дальность каскадов как на игровых картах
    sun = by_label.get("Sun_Login")
    if sun is not None:
        dc = sun.get_component_by_class(unreal.DirectionalLightComponent)
        was = float(dc.get_editor_property("dynamic_shadow_distance_movable_light"))
        dc.set_editor_property("dynamic_shadow_distance_movable_light", SUN_SHADOW)
        changed.append("Sun_Login: дальность динам. теней %.0f -> %.0f" % (was, SUN_SHADOW))
    else:
        log("ВНИМАНИЕ: Sun_Login не найден")

    # 3b) ★ НЕБО: sky_distance_threshold — это БЛИЖНЯЯ ПЛОСКОСТЬ захвата кубмапа у SLS_CAPTURED_SCENE
    #     (ReflectionEnvironmentCapture.cpp: в кубмап попадает только то, что ДАЛЬШЕ порога).
    #     Параметр масштабозависимый: движковый дефолт 150 000 в ×100-сцене отсекал ближнюю листву,
    #     но оставлял дальнюю оболочку задника (~400 000) ⇒ эмбиент был окрашен сценой. В ×1-сцене
    #     (габарит 7 880 юн) за 150 000 нет НИЧЕГО ⇒ кубмап пустой, эмбиент просел. Ставим пропорцию.
    sky = by_label.get("Sky_Login")
    if sky is not None:
        skc = sky.get_component_by_class(unreal.SkyLightComponent)
        was = float(skc.get_editor_property("sky_distance_threshold"))
        if abs(was - SKY_THR) > 1e-3:
            skc.set_editor_property("sky_distance_threshold", SKY_THR)
            try:
                skc.recapture_sky()     # пересобрать кубмап с новым порогом
            except Exception as e:
                log("recapture_sky не вызвался (%s) — кубмап пересоберётся при загрузке уровня" % e)
            changed.append("Sky_Login: sky_distance_threshold %.0f -> %.0f" % (was, SKY_THR))
        else:
            log("Sky_Login: sky_distance_threshold уже %.0f" % was)
    else:
        log("ВНИМАНИЕ: Sky_Login не найден")

    # контроль: дети шкатулки поехали за родителем (их мировой масштаб = отн. × масштаб шкатулки)
    for lbl, a in sorted(by_label.items()):
        if a.get_attach_parent_actor() is casket:
            log("ребёнок %s: мировой loc %s scale %s"
                % (lbl, vs(a.get_actor_location()), vs(a.get_actor_scale3d())))

    les.save_current_level()

    # 4) масштаб превью персонажа в конфиге сцены
    cfg = EAL.load_asset(CFG)
    if cfg is not None:
        was = float(cfg.get_editor_property("character_scale"))
        cfg.set_editor_property("character_scale", CHAR_SCALE)
        EAL.save_loaded_asset(cfg)
        changed.append("DA_LoginConfig.CharacterScale %.4f -> %.4f" % (was, CHAR_SCALE))
    else:
        log("ВНИМАНИЕ: не загрузился %s" % CFG)

    for c in changed:
        log(c)
    log("RESCALE-FINISHED (множитель %.6f, целевой масштаб %.4f юн/игр.ед.)" % (K, TARGET))


# ★ ЗАКОН [[tools-no-import-side-effects]]: при импорте модуль НИЧЕГО не делает.
if __name__ == "__main__" or globals().get("__spec__") is None:
    main()
